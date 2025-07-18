#include "data.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <QAbstractItemModel>

#include <apps/opencs/model/world/actoradapter.hpp>
#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/collectionbase.hpp>
#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/idtablebase.hpp>
#include <apps/opencs/model/world/info.hpp>
#include <apps/opencs/model/world/infocollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/metadata.hpp>
#include <apps/opencs/model/world/nestedidcollection.hpp>
#include <apps/opencs/model/world/nestedinfocollection.hpp>
#include <apps/opencs/model/world/pathgrid.hpp>
#include <apps/opencs/model/world/ref.hpp>
#include <apps/opencs/model/world/refcollection.hpp>
#include <apps/opencs/model/world/refidcollection.hpp>
#include <apps/opencs/model/world/subcellcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include <components/debug/debuglog.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/esmcommon.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/infoorder.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/files/collections.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include "../doc/messages.hpp"

#include "columnimp.hpp"
#include "columns.hpp"
#include "idtable.hpp"
#include "idtree.hpp"
#include "nestedcoladapterimp.hpp"
#include "regionmap.hpp"
#include "resourcesmanager.hpp"
#include "resourcetable.hpp"

namespace CSMWorld
{
    namespace
    {
        void removeDialogueInfos(
            const ESM::RefId& dialogueId, InfoOrderByTopic& infoOrders, InfoCollection& infoCollection)
        {
            const auto topicInfoOrder = infoOrders.find(dialogueId);

            if (topicInfoOrder == infoOrders.end())
                return;

            std::vector<int> erasedRecords;

            for (const OrderedInfo& info : topicInfoOrder->second.getOrderedInfo())
            {
                const ESM::RefId id = makeCompositeInfoRefId(dialogueId, info.mId);
                const Record<Info>& record = infoCollection.getRecord(id);

                if (record.mState == RecordBase::State_ModifiedOnly)
                {
                    erasedRecords.push_back(infoCollection.searchId(id));
                    continue;
                }

                auto deletedRecord = std::make_unique<Record<Info>>(record);
                deletedRecord->mState = RecordBase::State_Deleted;
                infoCollection.setRecord(infoCollection.searchId(id), std::move(deletedRecord));
            }

            while (!erasedRecords.empty())
            {
                infoCollection.removeRows(erasedRecords.back(), 1);
                erasedRecords.pop_back();
            }
        }
    }
}

void CSMWorld::Data::addModel(QAbstractItemModel* model, UniversalId::Type type, bool update)
{
    mModels.push_back(model);
    mModelIndex.insert(std::make_pair(type, model));

    UniversalId::Type type2 = UniversalId::getParentType(type);

    if (type2 != UniversalId::Type_None)
        mModelIndex.insert(std::make_pair(type2, model));

    if (update)
    {
        connect(model, &QAbstractItemModel::dataChanged, this, &Data::dataChanged);
        connect(model, &QAbstractItemModel::rowsInserted, this, &Data::rowsChanged);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &Data::rowsChanged);
    }
}

void CSMWorld::Data::appendIds(std::vector<ESM::RefId>& ids, const CollectionBase& collection, bool listDeleted)
{
    std::vector<ESM::RefId> ids2 = collection.getIds(listDeleted);

    ids.insert(ids.end(), ids2.begin(), ids2.end());
}

int CSMWorld::Data::count(RecordBase::State state, const CollectionBase& collection)
{
    int number = 0;

    for (int i = 0; i < collection.getSize(); ++i)
        if (collection.getRecord(i).mState == state)
            ++number;

    return number;
}

CSMWorld::Data::Data(ToUTF8::FromType encoding, const Files::PathContainer& dataPaths,
    const std::vector<std::string>& archives, const std::filesystem::path& resDir)
    : mEncoder(encoding)
    , mPathgrids(mCells)
    , mRefs(mCells)
    , mDialogue(nullptr)
    , mReaderIndex(0)
    , mDataPaths(dataPaths)
    , mArchives(archives)
    , mVFS(std::make_unique<VFS::Manager>())
{
    VFS::registerArchives(mVFS.get(), Files::Collections(mDataPaths), mArchives, true, &mEncoder.getStatelessEncoder());

    mResourcesManager.setVFS(mVFS.get());

    constexpr double expiryDelay = 0;
    mResourceSystem
        = std::make_unique<Resource::ResourceSystem>(mVFS.get(), expiryDelay, &mEncoder.getStatelessEncoder());

    Shader::ShaderManager::DefineMap defines
        = mResourceSystem->getSceneManager()->getShaderManager().getGlobalDefines();
    Shader::ShaderManager::DefineMap shadowDefines = SceneUtil::ShadowManager::getShadowsDisabledDefines();
    defines["forcePPL"] = "0"; // Don't force per-pixel lighting
    defines["clamp"] = "1"; // Clamp lighting
    defines["preLightEnv"] = "0"; // Apply environment maps after lighting like Morrowind
    defines["radialFog"] = "0";
    defines["lightingModel"] = "0";
    defines["reverseZ"] = "0";
    defines["waterRefraction"] = "0";
    for (const auto& define : shadowDefines)
        defines[define.first] = define.second;
    mResourceSystem->getSceneManager()->getShaderManager().setGlobalDefines(defines);

    mResourceSystem->getSceneManager()->setShaderPath(resDir / "shaders");

    int index = 0;

    mGlobals.addColumn(new StringIdColumn<ESM::Global>);
    mGlobals.addColumn(new RecordStateColumn<ESM::Global>);
    mGlobals.addColumn(new FixedRecordTypeColumn<ESM::Global>(UniversalId::Type_Global));
    mGlobals.addColumn(new VarTypeColumn<ESM::Global>(ColumnBase::Display_GlobalVarType));
    mGlobals.addColumn(new VarValueColumn<ESM::Global>);

    mGmsts.addColumn(new StringIdColumn<ESM::GameSetting>);
    mGmsts.addColumn(new RecordStateColumn<ESM::GameSetting>);
    mGmsts.addColumn(new FixedRecordTypeColumn<ESM::GameSetting>(UniversalId::Type_Gmst));
    mGmsts.addColumn(new VarTypeColumn<ESM::GameSetting>(ColumnBase::Display_GmstVarType));
    mGmsts.addColumn(new VarValueColumn<ESM::GameSetting>);

    mSkills.addColumn(new StringIdColumn<ESM::Skill>);
    mSkills.addColumn(new RecordStateColumn<ESM::Skill>);
    mSkills.addColumn(new FixedRecordTypeColumn<ESM::Skill>(UniversalId::Type_Skill));
    mSkills.addColumn(new AttributeColumn<ESM::Skill>);
    mSkills.addColumn(new SpecialisationColumn<ESM::Skill>);
    for (int i = 0; i < 4; ++i)
        mSkills.addColumn(new UseValueColumn<ESM::Skill>(i));
    mSkills.addColumn(new DescriptionColumn<ESM::Skill>);

    mClasses.addColumn(new StringIdColumn<ESM::Class>);
    mClasses.addColumn(new RecordStateColumn<ESM::Class>);
    mClasses.addColumn(new FixedRecordTypeColumn<ESM::Class>(UniversalId::Type_Class));
    mClasses.addColumn(new NameColumn<ESM::Class>);
    mClasses.addColumn(new AttributesColumn<ESM::Class>(0));
    mClasses.addColumn(new AttributesColumn<ESM::Class>(1));
    mClasses.addColumn(new SpecialisationColumn<ESM::Class>);
    for (int i = 0; i < 5; ++i)
        mClasses.addColumn(new SkillsColumn<ESM::Class>(i, true, true));
    for (int i = 0; i < 5; ++i)
        mClasses.addColumn(new SkillsColumn<ESM::Class>(i, true, false));
    mClasses.addColumn(new PlayableColumn<ESM::Class>);
    mClasses.addColumn(new DescriptionColumn<ESM::Class>);

    mFactions.addColumn(new StringIdColumn<ESM::Faction>);
    mFactions.addColumn(new RecordStateColumn<ESM::Faction>);
    mFactions.addColumn(new FixedRecordTypeColumn<ESM::Faction>(UniversalId::Type_Faction));
    mFactions.addColumn(new NameColumn<ESM::Faction>(ColumnBase::Display_String32));
    mFactions.addColumn(new AttributesColumn<ESM::Faction>(0));
    mFactions.addColumn(new AttributesColumn<ESM::Faction>(1));
    mFactions.addColumn(new HiddenColumn<ESM::Faction>);
    for (int i = 0; i < 7; ++i)
        mFactions.addColumn(new SkillsColumn<ESM::Faction>(i));
    // Faction Reactions
    mFactions.addColumn(new NestedParentColumn<ESM::Faction>(Columns::ColumnId_FactionReactions));
    index = mFactions.getColumns() - 1;
    mFactions.addAdapter(std::make_pair(&mFactions.getColumn(index), new FactionReactionsAdapter()));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Faction, ColumnBase::Display_Faction));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionReaction, ColumnBase::Display_Integer));

    // Faction Ranks
    mFactions.addColumn(new NestedParentColumn<ESM::Faction>(Columns::ColumnId_FactionRanks));
    index = mFactions.getColumns() - 1;
    mFactions.addAdapter(std::make_pair(&mFactions.getColumn(index), new FactionRanksAdapter()));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_RankName, ColumnBase::Display_Rank));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionAttrib1, ColumnBase::Display_Integer));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionAttrib2, ColumnBase::Display_Integer));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionPrimSkill, ColumnBase::Display_Integer));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionFavSkill, ColumnBase::Display_Integer));
    mFactions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FactionRep, ColumnBase::Display_Integer));

    mRaces.addColumn(new StringIdColumn<ESM::Race>);
    mRaces.addColumn(new RecordStateColumn<ESM::Race>);
    mRaces.addColumn(new FixedRecordTypeColumn<ESM::Race>(UniversalId::Type_Race));
    mRaces.addColumn(new NameColumn<ESM::Race>);
    mRaces.addColumn(new DescriptionColumn<ESM::Race>);
    mRaces.addColumn(new FlagColumn<ESM::Race>(Columns::ColumnId_Playable, 0x1));
    mRaces.addColumn(new FlagColumn<ESM::Race>(Columns::ColumnId_BeastRace, 0x2));
    mRaces.addColumn(new WeightHeightColumn<ESM::Race>(true, true));
    mRaces.addColumn(new WeightHeightColumn<ESM::Race>(true, false));
    mRaces.addColumn(new WeightHeightColumn<ESM::Race>(false, true));
    mRaces.addColumn(new WeightHeightColumn<ESM::Race>(false, false));
    // Race spells
    mRaces.addColumn(new NestedParentColumn<ESM::Race>(Columns::ColumnId_PowerList));
    index = mRaces.getColumns() - 1;
    mRaces.addAdapter(std::make_pair(&mRaces.getColumn(index), new SpellListAdapter<ESM::Race>()));
    mRaces.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_SpellId, ColumnBase::Display_Spell));
    // Race attributes
    mRaces.addColumn(new NestedParentColumn<ESM::Race>(
        Columns::ColumnId_RaceAttributes, ColumnBase::Flag_Dialogue, true)); // fixed rows table
    index = mRaces.getColumns() - 1;
    mRaces.addAdapter(std::make_pair(&mRaces.getColumn(index), new RaceAttributeAdapter()));
    mRaces.getNestableColumn(index)->addColumn(new NestedChildColumn(
        Columns::ColumnId_Attribute, ColumnBase::Display_Attribute, ColumnBase::Flag_Dialogue, false));
    mRaces.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Male, ColumnBase::Display_Integer));
    mRaces.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Female, ColumnBase::Display_Integer));
    // Race skill bonus
    mRaces.addColumn(new NestedParentColumn<ESM::Race>(
        Columns::ColumnId_RaceSkillBonus, ColumnBase::Flag_Dialogue, true)); // fixed rows table
    index = mRaces.getColumns() - 1;
    mRaces.addAdapter(std::make_pair(&mRaces.getColumn(index), new RaceSkillsBonusAdapter()));
    mRaces.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Skill, ColumnBase::Display_SkillId));
    mRaces.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_RaceBonus, ColumnBase::Display_Integer));

    mSounds.addColumn(new StringIdColumn<ESM::Sound>);
    mSounds.addColumn(new RecordStateColumn<ESM::Sound>);
    mSounds.addColumn(new FixedRecordTypeColumn<ESM::Sound>(UniversalId::Type_Sound));
    mSounds.addColumn(new SoundParamColumn<ESM::Sound>(SoundParamColumn<ESM::Sound>::Type_Volume));
    mSounds.addColumn(new SoundParamColumn<ESM::Sound>(SoundParamColumn<ESM::Sound>::Type_MinRange));
    mSounds.addColumn(new SoundParamColumn<ESM::Sound>(SoundParamColumn<ESM::Sound>::Type_MaxRange));
    mSounds.addColumn(new SoundFileColumn<ESM::Sound>);

    mScripts.addColumn(new StringIdColumn<ESM::Script>);
    mScripts.addColumn(new RecordStateColumn<ESM::Script>);
    mScripts.addColumn(new FixedRecordTypeColumn<ESM::Script>(UniversalId::Type_Script));
    mScripts.addColumn(new ScriptColumn<ESM::Script>(ScriptColumn<ESM::Script>::Type_File));

    mRegions.addColumn(new StringIdColumn<ESM::Region>);
    mRegions.addColumn(new RecordStateColumn<ESM::Region>);
    mRegions.addColumn(new FixedRecordTypeColumn<ESM::Region>(UniversalId::Type_Region));
    mRegions.addColumn(new NameColumn<ESM::Region>);
    mRegions.addColumn(new MapColourColumn<ESM::Region>);
    mRegions.addColumn(new SleepListColumn<ESM::Region>);
    // Region Weather
    mRegions.addColumn(new NestedParentColumn<ESM::Region>(Columns::ColumnId_RegionWeather));
    index = mRegions.getColumns() - 1;
    mRegions.addAdapter(std::make_pair(&mRegions.getColumn(index), new RegionWeatherAdapter()));
    mRegions.getNestableColumn(index)->addColumn(new NestedChildColumn(
        Columns::ColumnId_WeatherName, ColumnBase::Display_String, ColumnBase::Flag_Dialogue, false));
    mRegions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_WeatherChance, ColumnBase::Display_UnsignedInteger8));
    // Region Sounds
    mRegions.addColumn(new NestedParentColumn<ESM::Region>(Columns::ColumnId_RegionSounds));
    index = mRegions.getColumns() - 1;
    mRegions.addAdapter(std::make_pair(&mRegions.getColumn(index), new RegionSoundListAdapter()));
    mRegions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_SoundName, ColumnBase::Display_Sound));
    mRegions.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_SoundChance, ColumnBase::Display_UnsignedInteger8));
    mRegions.getNestableColumn(index)->addColumn(new NestedChildColumn(
        Columns::ColumnId_SoundProbability, ColumnBase::Display_String, ColumnBase::Flag_Dialogue, false));

    mBirthsigns.addColumn(new StringIdColumn<ESM::BirthSign>);
    mBirthsigns.addColumn(new RecordStateColumn<ESM::BirthSign>);
    mBirthsigns.addColumn(new FixedRecordTypeColumn<ESM::BirthSign>(UniversalId::Type_Birthsign));
    mBirthsigns.addColumn(new NameColumn<ESM::BirthSign>);
    mBirthsigns.addColumn(new TextureColumn<ESM::BirthSign>);
    mBirthsigns.addColumn(new DescriptionColumn<ESM::BirthSign>);
    // Birthsign spells
    mBirthsigns.addColumn(new NestedParentColumn<ESM::BirthSign>(Columns::ColumnId_PowerList));
    index = mBirthsigns.getColumns() - 1;
    mBirthsigns.addAdapter(std::make_pair(&mBirthsigns.getColumn(index), new SpellListAdapter<ESM::BirthSign>()));
    mBirthsigns.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_SpellId, ColumnBase::Display_Spell));

    mSpells.addColumn(new StringIdColumn<ESM::Spell>);
    mSpells.addColumn(new RecordStateColumn<ESM::Spell>);
    mSpells.addColumn(new FixedRecordTypeColumn<ESM::Spell>(UniversalId::Type_Spell));
    mSpells.addColumn(new NameColumn<ESM::Spell>);
    mSpells.addColumn(new SpellTypeColumn<ESM::Spell>);
    mSpells.addColumn(new CostColumn<ESM::Spell>);
    mSpells.addColumn(new FlagColumn<ESM::Spell>(Columns::ColumnId_AutoCalc, 0x1));
    mSpells.addColumn(new FlagColumn<ESM::Spell>(Columns::ColumnId_StarterSpell, 0x2));
    mSpells.addColumn(new FlagColumn<ESM::Spell>(Columns::ColumnId_AlwaysSucceeds, 0x4));
    // Spell effects
    mSpells.addColumn(new NestedParentColumn<ESM::Spell>(Columns::ColumnId_EffectList));
    index = mSpells.getColumns() - 1;
    mSpells.addAdapter(std::make_pair(&mSpells.getColumn(index), new EffectsListAdapter<ESM::Spell>()));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectId, ColumnBase::Display_EffectId));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Skill, ColumnBase::Display_EffectSkill));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Attribute, ColumnBase::Display_EffectAttribute));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectRange, ColumnBase::Display_EffectRange));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectArea, ColumnBase::Display_String));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Duration, ColumnBase::Display_Integer)); // reuse from light
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_MinMagnitude, ColumnBase::Display_Integer));
    mSpells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_MaxMagnitude, ColumnBase::Display_Integer));

    mTopics.addColumn(new StringIdColumn<ESM::Dialogue>);
    mTopics.addColumn(new RecordStateColumn<ESM::Dialogue>);
    mTopics.addColumn(new FixedRecordTypeColumn<ESM::Dialogue>(UniversalId::Type_Topic));
    mTopics.addColumn(new DialogueTypeColumn<ESM::Dialogue>);

    mJournals.addColumn(new StringIdColumn<ESM::Dialogue>);
    mJournals.addColumn(new RecordStateColumn<ESM::Dialogue>);
    mJournals.addColumn(new FixedRecordTypeColumn<ESM::Dialogue>(UniversalId::Type_Journal));
    mJournals.addColumn(new DialogueTypeColumn<ESM::Dialogue>(true));

    mTopicInfos.addColumn(new StringIdColumn<Info>(true));
    mTopicInfos.addColumn(new RecordStateColumn<Info>);
    mTopicInfos.addColumn(new FixedRecordTypeColumn<Info>(UniversalId::Type_TopicInfo));
    mTopicInfos.addColumn(new TopicColumn<Info>(false));
    mTopicInfos.addColumn(new ResponseColumn<Info>);
    mTopicInfos.addColumn(new ActorColumn<Info>);
    mTopicInfos.addColumn(new RaceColumn<Info>);
    mTopicInfos.addColumn(new ClassColumn<Info>);
    mTopicInfos.addColumn(new FactionColumn<Info>);
    mTopicInfos.addColumn(new CellColumn<Info>);
    mTopicInfos.addColumn(new DispositionColumn<Info>);
    mTopicInfos.addColumn(new RankColumn<Info>);
    mTopicInfos.addColumn(new GenderColumn<Info>);
    mTopicInfos.addColumn(new PcFactionColumn<Info>);
    mTopicInfos.addColumn(new PcRankColumn<Info>);
    mTopicInfos.addColumn(new SoundFileColumn<Info>);
    // Result script
    mTopicInfos.addColumn(new NestedParentColumn<Info>(
        Columns::ColumnId_InfoList, ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_List));
    index = mTopicInfos.getColumns() - 1;
    mTopicInfos.addAdapter(std::make_pair(&mTopicInfos.getColumn(index), new InfoListAdapter()));
    mTopicInfos.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_ScriptText, ColumnBase::Display_ScriptLines));
    // Special conditions
    mTopicInfos.addColumn(new NestedParentColumn<Info>(Columns::ColumnId_InfoCondition));
    index = mTopicInfos.getColumns() - 1;
    mTopicInfos.addAdapter(std::make_pair(&mTopicInfos.getColumn(index), new InfoConditionAdapter()));
    mTopicInfos.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_InfoCondFunc, ColumnBase::Display_InfoCondFunc));
    // FIXME: don't have dynamic value enum delegate, use Display_String for now
    mTopicInfos.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_InfoCondVar, ColumnBase::Display_InfoCondVar));
    mTopicInfos.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_InfoCondComp, ColumnBase::Display_InfoCondComp));
    mTopicInfos.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Value, ColumnBase::Display_Var));

    mJournalInfos.addColumn(new StringIdColumn<Info>(true));
    mJournalInfos.addColumn(new RecordStateColumn<Info>);
    mJournalInfos.addColumn(new FixedRecordTypeColumn<Info>(UniversalId::Type_JournalInfo));
    mJournalInfos.addColumn(new TopicColumn<Info>(true));
    mJournalInfos.addColumn(new QuestStatusTypeColumn<Info>);
    mJournalInfos.addColumn(new QuestIndexColumn<Info>);
    mJournalInfos.addColumn(new QuestDescriptionColumn<Info>);

    mCells.addColumn(new StringIdColumn<Cell>);
    mCells.addColumn(new RecordStateColumn<Cell>);
    mCells.addColumn(new FixedRecordTypeColumn<Cell>(UniversalId::Type_Cell));
    mCells.addColumn(new NameColumn<Cell>(ColumnBase::Display_String64));
    mCells.addColumn(new FlagColumn<Cell>(Columns::ColumnId_SleepForbidden, ESM::Cell::NoSleep));
    mCells.addColumn(new FlagColumn<Cell>(Columns::ColumnId_InteriorWater, ESM::Cell::HasWater,
        ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh));
    mCells.addColumn(new FlagColumn<Cell>(Columns::ColumnId_InteriorSky, ESM::Cell::QuasiEx,
        ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh));
    mCells.addColumn(new RegionColumn<Cell>);
    mCells.addColumn(new RefNumCounterColumn<Cell>);
    // Misc Cell data
    mCells.addColumn(new NestedParentColumn<Cell>(
        Columns::ColumnId_Cell, ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_List));
    index = mCells.getColumns() - 1;
    mCells.addAdapter(std::make_pair(&mCells.getColumn(index), new CellListAdapter()));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Interior, ColumnBase::Display_Boolean,
            ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Ambient, ColumnBase::Display_Colour));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Sunlight, ColumnBase::Display_Colour));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Fog, ColumnBase::Display_Colour));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_FogDensity, ColumnBase::Display_Float));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_WaterLevel, ColumnBase::Display_Float));
    mCells.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_MapColor, ColumnBase::Display_Colour));

    mEnchantments.addColumn(new StringIdColumn<ESM::Enchantment>);
    mEnchantments.addColumn(new RecordStateColumn<ESM::Enchantment>);
    mEnchantments.addColumn(new FixedRecordTypeColumn<ESM::Enchantment>(UniversalId::Type_Enchantment));
    mEnchantments.addColumn(new EnchantmentTypeColumn<ESM::Enchantment>);
    mEnchantments.addColumn(new CostColumn<ESM::Enchantment>);
    mEnchantments.addColumn(new ChargesColumn2<ESM::Enchantment>);
    mEnchantments.addColumn(new FlagColumn<ESM::Enchantment>(Columns::ColumnId_AutoCalc, ESM::Enchantment::Autocalc));
    // Enchantment effects
    mEnchantments.addColumn(new NestedParentColumn<ESM::Enchantment>(Columns::ColumnId_EffectList));
    index = mEnchantments.getColumns() - 1;
    mEnchantments.addAdapter(
        std::make_pair(&mEnchantments.getColumn(index), new EffectsListAdapter<ESM::Enchantment>()));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectId, ColumnBase::Display_EffectId));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Skill, ColumnBase::Display_EffectSkill));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Attribute, ColumnBase::Display_EffectAttribute));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectRange, ColumnBase::Display_EffectRange));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_EffectArea, ColumnBase::Display_String));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_Duration, ColumnBase::Display_Integer)); // reuse from light
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_MinMagnitude, ColumnBase::Display_Integer));
    mEnchantments.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_MaxMagnitude, ColumnBase::Display_Integer));

    mBodyParts.addColumn(new StringIdColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new RecordStateColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new FixedRecordTypeColumn<ESM::BodyPart>(UniversalId::Type_BodyPart));
    mBodyParts.addColumn(new BodyPartTypeColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new VampireColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new GenderNpcColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new FlagColumn<ESM::BodyPart>(Columns::ColumnId_Playable, ESM::BodyPart::BPF_NotPlayable,
        ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue, true));

    int meshTypeFlags = ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh;
    MeshTypeColumn<ESM::BodyPart>* meshTypeColumn = new MeshTypeColumn<ESM::BodyPart>(meshTypeFlags);
    mBodyParts.addColumn(meshTypeColumn);
    mBodyParts.addColumn(new ModelColumn<ESM::BodyPart>);
    mBodyParts.addColumn(new BodyPartRaceColumn(meshTypeColumn));

    mSoundGens.addColumn(new StringIdColumn<ESM::SoundGenerator>);
    mSoundGens.addColumn(new RecordStateColumn<ESM::SoundGenerator>);
    mSoundGens.addColumn(new FixedRecordTypeColumn<ESM::SoundGenerator>(UniversalId::Type_SoundGen));
    mSoundGens.addColumn(new CreatureColumn<ESM::SoundGenerator>);
    mSoundGens.addColumn(new SoundColumn<ESM::SoundGenerator>);
    mSoundGens.addColumn(new SoundGeneratorTypeColumn<ESM::SoundGenerator>);

    mMagicEffects.addColumn(new StringIdColumn<ESM::MagicEffect>);
    mMagicEffects.addColumn(new RecordStateColumn<ESM::MagicEffect>);
    mMagicEffects.addColumn(new FixedRecordTypeColumn<ESM::MagicEffect>(UniversalId::Type_MagicEffect));
    mMagicEffects.addColumn(new SchoolColumn<ESM::MagicEffect>);
    mMagicEffects.addColumn(new BaseCostColumn<ESM::MagicEffect>);
    mMagicEffects.addColumn(new ProjectileSpeedColumn<ESM::MagicEffect>);
    mMagicEffects.addColumn(new EffectTextureColumn<ESM::MagicEffect>(Columns::ColumnId_Icon));
    mMagicEffects.addColumn(new EffectTextureColumn<ESM::MagicEffect>(Columns::ColumnId_Particle));
    mMagicEffects.addColumn(new EffectObjectColumn<ESM::MagicEffect>(Columns::ColumnId_CastingObject));
    mMagicEffects.addColumn(new EffectObjectColumn<ESM::MagicEffect>(Columns::ColumnId_HitObject));
    mMagicEffects.addColumn(new EffectObjectColumn<ESM::MagicEffect>(Columns::ColumnId_AreaObject));
    mMagicEffects.addColumn(new EffectObjectColumn<ESM::MagicEffect>(Columns::ColumnId_BoltObject));
    mMagicEffects.addColumn(new EffectSoundColumn<ESM::MagicEffect>(Columns::ColumnId_CastingSound));
    mMagicEffects.addColumn(new EffectSoundColumn<ESM::MagicEffect>(Columns::ColumnId_HitSound));
    mMagicEffects.addColumn(new EffectSoundColumn<ESM::MagicEffect>(Columns::ColumnId_AreaSound));
    mMagicEffects.addColumn(new EffectSoundColumn<ESM::MagicEffect>(Columns::ColumnId_BoltSound));

    mMagicEffects.addColumn(
        new FlagColumn<ESM::MagicEffect>(Columns::ColumnId_AllowSpellmaking, ESM::MagicEffect::AllowSpellmaking));
    mMagicEffects.addColumn(
        new FlagColumn<ESM::MagicEffect>(Columns::ColumnId_AllowEnchanting, ESM::MagicEffect::AllowEnchanting));
    mMagicEffects.addColumn(
        new FlagColumn<ESM::MagicEffect>(Columns::ColumnId_NegativeLight, ESM::MagicEffect::NegativeLight));
    mMagicEffects.addColumn(new DescriptionColumn<ESM::MagicEffect>);

    mLand.addColumn(new StringIdColumn<Land>);
    mLand.addColumn(new RecordStateColumn<Land>);
    mLand.addColumn(new FixedRecordTypeColumn<Land>(UniversalId::Type_Land));
    mLand.addColumn(new LandPluginIndexColumn);
    mLand.addColumn(new LandNormalsColumn);
    mLand.addColumn(new LandHeightsColumn);
    mLand.addColumn(new LandColoursColumn);
    mLand.addColumn(new LandTexturesColumn);

    mLandTextures.addColumn(new StringIdColumn<ESM::LandTexture>);
    mLandTextures.addColumn(new RecordStateColumn<ESM::LandTexture>);
    mLandTextures.addColumn(new FixedRecordTypeColumn<ESM::LandTexture>(UniversalId::Type_LandTexture));
    mLandTextures.addColumn(new LandTextureIndexColumn);
    mLandTextures.addColumn(new TextureColumn<ESM::LandTexture>);

    mPathgrids.addColumn(new StringIdColumn<Pathgrid>);
    mPathgrids.addColumn(new RecordStateColumn<Pathgrid>);
    mPathgrids.addColumn(new FixedRecordTypeColumn<Pathgrid>(UniversalId::Type_Pathgrid));

    // new object deleted in dtor of Collection<T,A>
    mPathgrids.addColumn(new NestedParentColumn<Pathgrid>(Columns::ColumnId_PathgridPoints));
    index = mPathgrids.getColumns() - 1;
    // new object deleted in dtor of NestedCollection<T,A>
    mPathgrids.addAdapter(std::make_pair(&mPathgrids.getColumn(index), new PathgridPointListAdapter()));
    // new objects deleted in dtor of NestableColumn
    // WARNING: The order of the columns below are assumed in PathgridPointListAdapter
    mPathgrids.getNestableColumn(index)->addColumn(new NestedChildColumn(
        Columns::ColumnId_PathgridIndex, ColumnBase::Display_Integer, ColumnBase::Flag_Dialogue, false));
    mPathgrids.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_PathgridPosX, ColumnBase::Display_Integer));
    mPathgrids.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_PathgridPosY, ColumnBase::Display_Integer));
    mPathgrids.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_PathgridPosZ, ColumnBase::Display_Integer));

    mPathgrids.addColumn(new NestedParentColumn<Pathgrid>(Columns::ColumnId_PathgridEdges));
    index = mPathgrids.getColumns() - 1;
    mPathgrids.addAdapter(std::make_pair(&mPathgrids.getColumn(index), new PathgridEdgeListAdapter()));
    mPathgrids.getNestableColumn(index)->addColumn(new NestedChildColumn(
        Columns::ColumnId_PathgridEdgeIndex, ColumnBase::Display_Integer, ColumnBase::Flag_Dialogue, false));
    mPathgrids.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_PathgridEdge0, ColumnBase::Display_Integer));
    mPathgrids.getNestableColumn(index)->addColumn(
        new NestedChildColumn(Columns::ColumnId_PathgridEdge1, ColumnBase::Display_Integer));

    mStartScripts.addColumn(new StringIdColumn<ESM::StartScript>);
    mStartScripts.addColumn(new RecordStateColumn<ESM::StartScript>);
    mStartScripts.addColumn(new FixedRecordTypeColumn<ESM::StartScript>(UniversalId::Type_StartScript));

    mRefs.addColumn(new StringIdColumn<CellRef>(true));
    mRefs.addColumn(new RecordStateColumn<CellRef>);
    mRefs.addColumn(new FixedRecordTypeColumn<CellRef>(UniversalId::Type_Reference));
    mRefs.addColumn(new CellColumn<CellRef>(true));
    mRefs.addColumn(new OriginalCellColumn<CellRef>);
    mRefs.addColumn(new IdColumn<CellRef>);
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mPos, 0, false));
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mPos, 1, false));
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mPos, 2, false));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mPos, 0, false));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mPos, 1, false));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mPos, 2, false));
    mRefs.addColumn(new ScaleColumn<CellRef>);
    mRefs.addColumn(new OwnerColumn<CellRef>);
    mRefs.addColumn(new SoulColumn<CellRef>);
    mRefs.addColumn(new FactionColumn<CellRef>);
    mRefs.addColumn(new FactionIndexColumn<CellRef>);
    mRefs.addColumn(new ChargesColumn<CellRef>);
    mRefs.addColumn(new EnchantmentChargesColumn<CellRef>);
    mRefs.addColumn(new StackSizeColumn<CellRef>);
    mRefs.addColumn(new TeleportColumn<CellRef>(
        ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh));
    mRefs.addColumn(new TeleportCellColumn<CellRef>);
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mDoorDest, 0, true));
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mDoorDest, 1, true));
    mRefs.addColumn(new PosColumn<CellRef>(&CellRef::mDoorDest, 2, true));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mDoorDest, 0, true));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mDoorDest, 1, true));
    mRefs.addColumn(new RotColumn<CellRef>(&CellRef::mDoorDest, 2, true));
    mRefs.addColumn(new IsLockedColumn<CellRef>(
        ColumnBase::Flag_Table | ColumnBase::Flag_Dialogue | ColumnBase::Flag_Dialogue_Refresh));
    mRefs.addColumn(new LockLevelColumn<CellRef>);
    mRefs.addColumn(new KeyColumn<CellRef>);
    mRefs.addColumn(new TrapColumn<CellRef>);
    mRefs.addColumn(new OwnerGlobalColumn<CellRef>);
    mRefs.addColumn(new RefNumColumn<CellRef>);

    mFilters.addColumn(new StringIdColumn<ESM::Filter>);
    mFilters.addColumn(new RecordStateColumn<ESM::Filter>);
    mFilters.addColumn(new FixedRecordTypeColumn<ESM::Filter>(UniversalId::Type_Filter));
    mFilters.addColumn(new FilterColumn<ESM::Filter>);
    mFilters.addColumn(new DescriptionColumn<ESM::Filter>);

    mDebugProfiles.addColumn(new StringIdColumn<ESM::DebugProfile>);
    mDebugProfiles.addColumn(new RecordStateColumn<ESM::DebugProfile>);
    mDebugProfiles.addColumn(new FixedRecordTypeColumn<ESM::DebugProfile>(UniversalId::Type_DebugProfile));
    mDebugProfiles.addColumn(
        new FlagColumn2<ESM::DebugProfile>(Columns::ColumnId_DefaultProfile, ESM::DebugProfile::Flag_Default));
    mDebugProfiles.addColumn(
        new FlagColumn2<ESM::DebugProfile>(Columns::ColumnId_BypassNewGame, ESM::DebugProfile::Flag_BypassNewGame));
    mDebugProfiles.addColumn(
        new FlagColumn2<ESM::DebugProfile>(Columns::ColumnId_GlobalProfile, ESM::DebugProfile::Flag_Global));
    mDebugProfiles.addColumn(new DescriptionColumn<ESM::DebugProfile>);
    mDebugProfiles.addColumn(new ScriptColumn<ESM::DebugProfile>(ScriptColumn<ESM::DebugProfile>::Type_Lines));

    mSelectionGroups.addColumn(new StringIdColumn<ESM::SelectionGroup>);
    mSelectionGroups.addColumn(new RecordStateColumn<ESM::SelectionGroup>);
    mSelectionGroups.addColumn(new FixedRecordTypeColumn<ESM::SelectionGroup>(UniversalId::Type_SelectionGroup));
    mSelectionGroups.addColumn(new SelectionGroupColumn);

    mMetaData.appendBlankRecord(ESM::RefId::stringRefId("sys::meta"));

    mMetaData.addColumn(new StringIdColumn<MetaData>(true));
    mMetaData.addColumn(new RecordStateColumn<MetaData>);
    mMetaData.addColumn(new FixedRecordTypeColumn<MetaData>(UniversalId::Type_MetaData));
    mMetaData.addColumn(new FormatColumn<MetaData>);
    mMetaData.addColumn(new AuthorColumn<MetaData>);
    mMetaData.addColumn(new FileDescriptionColumn<MetaData>);

    addModel(new IdTable(&mGlobals), UniversalId::Type_Global);
    addModel(new IdTable(&mGmsts), UniversalId::Type_Gmst);
    addModel(new IdTable(&mSkills), UniversalId::Type_Skill);
    addModel(new IdTable(&mClasses), UniversalId::Type_Class);
    addModel(new IdTree(&mFactions, &mFactions), UniversalId::Type_Faction);
    addModel(new IdTree(&mRaces, &mRaces), UniversalId::Type_Race);
    addModel(new IdTable(&mSounds), UniversalId::Type_Sound);
    addModel(new IdTable(&mScripts), UniversalId::Type_Script);
    addModel(new IdTree(&mRegions, &mRegions), UniversalId::Type_Region);
    addModel(new IdTree(&mBirthsigns, &mBirthsigns), UniversalId::Type_Birthsign);
    addModel(new IdTree(&mSpells, &mSpells), UniversalId::Type_Spell);
    addModel(new IdTable(&mTopics), UniversalId::Type_Topic);
    addModel(new IdTable(&mJournals), UniversalId::Type_Journal);
    addModel(new IdTree(&mTopicInfos, &mTopicInfos, IdTable::Feature_ReorderWithinTopic), UniversalId::Type_TopicInfo);
    addModel(new IdTable(&mJournalInfos, IdTable::Feature_ReorderWithinTopic), UniversalId::Type_JournalInfo);
    addModel(new IdTree(&mCells, &mCells, IdTable::Feature_ViewId), UniversalId::Type_Cell);
    addModel(new IdTree(&mEnchantments, &mEnchantments), UniversalId::Type_Enchantment);
    addModel(new IdTable(&mBodyParts), UniversalId::Type_BodyPart);
    addModel(new IdTable(&mSoundGens), UniversalId::Type_SoundGen);
    addModel(new IdTable(&mMagicEffects), UniversalId::Type_MagicEffect);
    addModel(new IdTable(&mLand, IdTable::Feature_AllowTouch), UniversalId::Type_Land);
    addModel(new LandTextureIdTable(&mLandTextures), UniversalId::Type_LandTexture);
    addModel(new IdTree(&mPathgrids, &mPathgrids), UniversalId::Type_Pathgrid);
    addModel(new IdTable(&mStartScripts), UniversalId::Type_StartScript);
    addModel(new IdTree(&mReferenceables, &mReferenceables, IdTable::Feature_Preview), UniversalId::Type_Referenceable);
    addModel(new IdTable(&mRefs, IdTable::Feature_ViewCell | IdTable::Feature_Preview), UniversalId::Type_Reference);
    addModel(new IdTable(&mFilters), UniversalId::Type_Filter);
    addModel(new IdTable(&mDebugProfiles), UniversalId::Type_DebugProfile);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_Meshes)), UniversalId::Type_Mesh);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_Icons)), UniversalId::Type_Icon);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_Musics)), UniversalId::Type_Music);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_SoundsRes)), UniversalId::Type_SoundRes);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_Textures)), UniversalId::Type_Texture);
    addModel(new ResourceTable(&mResourcesManager.get(UniversalId::Type_Videos)), UniversalId::Type_Video);
    addModel(new IdTable(&mMetaData), UniversalId::Type_MetaData);
    addModel(new IdTable(&mSelectionGroups), UniversalId::Type_SelectionGroup);

    mActorAdapter = std::make_unique<ActorAdapter>(*this);

    mRefLoadCache.clear(); // clear here rather than startLoading() and continueLoading() for multiple content files
}

CSMWorld::Data::~Data()
{
    for (std::vector<QAbstractItemModel*>::iterator iter(mModels.begin()); iter != mModels.end(); ++iter)
        delete *iter;
}

std::shared_ptr<Resource::ResourceSystem> CSMWorld::Data::getResourceSystem()
{
    return mResourceSystem;
}

std::shared_ptr<const Resource::ResourceSystem> CSMWorld::Data::getResourceSystem() const
{
    return mResourceSystem;
}

const CSMWorld::IdCollection<ESM::Global>& CSMWorld::Data::getGlobals() const
{
    return mGlobals;
}

CSMWorld::IdCollection<ESM::Global>& CSMWorld::Data::getGlobals()
{
    return mGlobals;
}

const CSMWorld::IdCollection<ESM::GameSetting>& CSMWorld::Data::getGmsts() const
{
    return mGmsts;
}

CSMWorld::IdCollection<ESM::GameSetting>& CSMWorld::Data::getGmsts()
{
    return mGmsts;
}

const CSMWorld::IdCollection<ESM::Skill>& CSMWorld::Data::getSkills() const
{
    return mSkills;
}

CSMWorld::IdCollection<ESM::Skill>& CSMWorld::Data::getSkills()
{
    return mSkills;
}

const CSMWorld::IdCollection<ESM::Class>& CSMWorld::Data::getClasses() const
{
    return mClasses;
}

CSMWorld::IdCollection<ESM::Class>& CSMWorld::Data::getClasses()
{
    return mClasses;
}

const CSMWorld::IdCollection<ESM::Faction>& CSMWorld::Data::getFactions() const
{
    return mFactions;
}

CSMWorld::IdCollection<ESM::Faction>& CSMWorld::Data::getFactions()
{
    return mFactions;
}

const CSMWorld::IdCollection<ESM::Race>& CSMWorld::Data::getRaces() const
{
    return mRaces;
}

CSMWorld::IdCollection<ESM::Race>& CSMWorld::Data::getRaces()
{
    return mRaces;
}

const CSMWorld::IdCollection<ESM::Sound>& CSMWorld::Data::getSounds() const
{
    return mSounds;
}

CSMWorld::IdCollection<ESM::Sound>& CSMWorld::Data::getSounds()
{
    return mSounds;
}

const CSMWorld::IdCollection<ESM::Script>& CSMWorld::Data::getScripts() const
{
    return mScripts;
}

CSMWorld::IdCollection<ESM::Script>& CSMWorld::Data::getScripts()
{
    return mScripts;
}

const CSMWorld::IdCollection<ESM::Region>& CSMWorld::Data::getRegions() const
{
    return mRegions;
}

CSMWorld::IdCollection<ESM::Region>& CSMWorld::Data::getRegions()
{
    return mRegions;
}

const CSMWorld::IdCollection<ESM::BirthSign>& CSMWorld::Data::getBirthsigns() const
{
    return mBirthsigns;
}

CSMWorld::IdCollection<ESM::BirthSign>& CSMWorld::Data::getBirthsigns()
{
    return mBirthsigns;
}

const CSMWorld::IdCollection<ESM::Spell>& CSMWorld::Data::getSpells() const
{
    return mSpells;
}

CSMWorld::IdCollection<ESM::Spell>& CSMWorld::Data::getSpells()
{
    return mSpells;
}

const CSMWorld::IdCollection<ESM::Dialogue>& CSMWorld::Data::getTopics() const
{
    return mTopics;
}

CSMWorld::IdCollection<ESM::Dialogue>& CSMWorld::Data::getTopics()
{
    return mTopics;
}

const CSMWorld::IdCollection<ESM::Dialogue>& CSMWorld::Data::getJournals() const
{
    return mJournals;
}

CSMWorld::IdCollection<ESM::Dialogue>& CSMWorld::Data::getJournals()
{
    return mJournals;
}

const CSMWorld::InfoCollection& CSMWorld::Data::getTopicInfos() const
{
    return mTopicInfos;
}

CSMWorld::InfoCollection& CSMWorld::Data::getTopicInfos()
{
    return mTopicInfos;
}

const CSMWorld::InfoCollection& CSMWorld::Data::getJournalInfos() const
{
    return mJournalInfos;
}

CSMWorld::InfoCollection& CSMWorld::Data::getJournalInfos()
{
    return mJournalInfos;
}

const CSMWorld::IdCollection<CSMWorld::Cell>& CSMWorld::Data::getCells() const
{
    return mCells;
}

CSMWorld::IdCollection<CSMWorld::Cell>& CSMWorld::Data::getCells()
{
    return mCells;
}

const CSMWorld::RefIdCollection& CSMWorld::Data::getReferenceables() const
{
    return mReferenceables;
}

CSMWorld::RefIdCollection& CSMWorld::Data::getReferenceables()
{
    return mReferenceables;
}

const CSMWorld::RefCollection& CSMWorld::Data::getReferences() const
{
    return mRefs;
}

CSMWorld::RefCollection& CSMWorld::Data::getReferences()
{
    return mRefs;
}

const CSMWorld::IdCollection<ESM::Filter>& CSMWorld::Data::getFilters() const
{
    return mFilters;
}

CSMWorld::IdCollection<ESM::Filter>& CSMWorld::Data::getFilters()
{
    return mFilters;
}

const CSMWorld::IdCollection<ESM::Enchantment>& CSMWorld::Data::getEnchantments() const
{
    return mEnchantments;
}

CSMWorld::IdCollection<ESM::Enchantment>& CSMWorld::Data::getEnchantments()
{
    return mEnchantments;
}

const CSMWorld::IdCollection<ESM::BodyPart>& CSMWorld::Data::getBodyParts() const
{
    return mBodyParts;
}

CSMWorld::IdCollection<ESM::BodyPart>& CSMWorld::Data::getBodyParts()
{
    return mBodyParts;
}

const CSMWorld::IdCollection<ESM::DebugProfile>& CSMWorld::Data::getDebugProfiles() const
{
    return mDebugProfiles;
}

CSMWorld::IdCollection<ESM::DebugProfile>& CSMWorld::Data::getDebugProfiles()
{
    return mDebugProfiles;
}

CSMWorld::IdCollection<ESM::SelectionGroup>& CSMWorld::Data::getSelectionGroups()
{
    return mSelectionGroups;
}

const CSMWorld::IdCollection<ESM::SelectionGroup>& CSMWorld::Data::getSelectionGroups() const
{
    return mSelectionGroups;
}

const CSMWorld::IdCollection<CSMWorld::Land>& CSMWorld::Data::getLand() const
{
    return mLand;
}

CSMWorld::IdCollection<CSMWorld::Land>& CSMWorld::Data::getLand()
{
    return mLand;
}

const CSMWorld::IdCollection<ESM::LandTexture>& CSMWorld::Data::getLandTextures() const
{
    return mLandTextures;
}

CSMWorld::IdCollection<ESM::LandTexture>& CSMWorld::Data::getLandTextures()
{
    return mLandTextures;
}

const CSMWorld::IdCollection<ESM::SoundGenerator>& CSMWorld::Data::getSoundGens() const
{
    return mSoundGens;
}

CSMWorld::IdCollection<ESM::SoundGenerator>& CSMWorld::Data::getSoundGens()
{
    return mSoundGens;
}

const CSMWorld::IdCollection<ESM::MagicEffect>& CSMWorld::Data::getMagicEffects() const
{
    return mMagicEffects;
}

CSMWorld::IdCollection<ESM::MagicEffect>& CSMWorld::Data::getMagicEffects()
{
    return mMagicEffects;
}

const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& CSMWorld::Data::getPathgrids() const
{
    return mPathgrids;
}

CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& CSMWorld::Data::getPathgrids()
{
    return mPathgrids;
}

const CSMWorld::IdCollection<ESM::StartScript>& CSMWorld::Data::getStartScripts() const
{
    return mStartScripts;
}

CSMWorld::IdCollection<ESM::StartScript>& CSMWorld::Data::getStartScripts()
{
    return mStartScripts;
}

const CSMWorld::Resources& CSMWorld::Data::getResources(const UniversalId& id) const
{
    return mResourcesManager.get(id.getType());
}

const CSMWorld::MetaData& CSMWorld::Data::getMetaData() const
{
    return mMetaData.getRecord(0).get();
}

void CSMWorld::Data::setMetaData(const MetaData& metaData)
{
    mMetaData.setRecord(
        0, std::make_unique<Record<MetaData>>(Record<MetaData>(RecordBase::State_ModifiedOnly, nullptr, &metaData)));
}

QAbstractItemModel* CSMWorld::Data::getTableModel(const CSMWorld::UniversalId& id)
{
    std::map<UniversalId::Type, QAbstractItemModel*>::iterator iter = mModelIndex.find(id.getType());

    if (iter == mModelIndex.end())
    {
        // try creating missing (secondary) tables on the fly
        //
        // Note: We create these tables here so we don't have to deal with them during load/initial
        // construction of the ESX data where no update signals are available.
        if (id.getType() == UniversalId::Type_RegionMap)
        {
            RegionMap* table = nullptr;
            addModel(table = new RegionMap(*this), UniversalId::Type_RegionMap, false);
            return table;
        }
        throw std::logic_error("No table model available for " + id.toString());
    }

    return iter->second;
}

const CSMWorld::ActorAdapter* CSMWorld::Data::getActorAdapter() const
{
    return mActorAdapter.get();
}

CSMWorld::ActorAdapter* CSMWorld::Data::getActorAdapter()
{
    return mActorAdapter.get();
}

void CSMWorld::Data::merge()
{
    mGlobals.merge();
}

int CSMWorld::Data::getTotalRecords(const std::vector<std::filesystem::path>& files)
{
    int records = 0;

    std::unique_ptr<ESM::ESMReader> reader = std::make_unique<ESM::ESMReader>();

    for (const auto& file : files)
    {
        if (!std::filesystem::exists(file))
            continue;

        reader->open(file);
        records += reader->getRecordCount();
        reader->close();
    }

    return records;
}

int CSMWorld::Data::startLoading(const std::filesystem::path& path, bool base, bool project)
{
    Log(Debug::Info) << "Loading content file " << path;

    mDialogue = nullptr;

    ESM::ReadersCache::BusyItem reader = mReaders.get(mReaderIndex++);
    reader->setEncoder(&mEncoder);
    reader->open(path);

    mBase = base;
    mProject = project;

    if (!mProject && !mBase)
    {
        MetaData metaData;
        metaData.mId = ESM::RefId::stringRefId("sys::meta");
        metaData.load(*reader);

        mMetaData.setRecord(0,
            std::make_unique<Record<MetaData>>(Record<MetaData>(RecordBase::State_ModifiedOnly, nullptr, &metaData)));
    }

    return reader->getRecordCount();
}

void CSMWorld::Data::loadFallbackEntries()
{
    // Load default marker definitions, if game files do not have them for some reason
    std::pair<std::string, std::string> staticMarkers[] = { std::make_pair("DivineMarker", "marker_divine.nif"),
        std::make_pair("DoorMarker", "marker_arrow.nif"), std::make_pair("NorthMarker", "marker_north.nif"),
        std::make_pair("TempleMarker", "marker_temple.nif"), std::make_pair("TravelMarker", "marker_travel.nif") };

    std::pair<std::string, std::string> doorMarkers[] = { std::make_pair("PrisonMarker", "marker_prison.nif") };

    for (const auto& [id, model] : staticMarkers)
    {
        const ESM::RefId refId = ESM::RefId::stringRefId(id);
        if (mReferenceables.searchId(refId) == -1)
        {
            ESM::Static newMarker;
            newMarker.mId = refId;
            newMarker.mModel = model;
            newMarker.mRecordFlags = 0;
            auto record = std::make_unique<CSMWorld::Record<ESM::Static>>();
            record->mBase = std::move(newMarker);
            record->mState = CSMWorld::RecordBase::State_BaseOnly;
            mReferenceables.appendRecord(std::move(record), CSMWorld::UniversalId::Type_Static);
        }
    }

    for (const auto& [id, model] : doorMarkers)
    {
        const ESM::RefId refId = ESM::RefId::stringRefId(id);
        if (mReferenceables.searchId(refId) == -1)
        {
            ESM::Door newMarker;
            newMarker.mId = refId;
            newMarker.mModel = model;
            newMarker.mRecordFlags = 0;
            auto record = std::make_unique<CSMWorld::Record<ESM::Door>>();
            record->mBase = std::move(newMarker);
            record->mState = CSMWorld::RecordBase::State_BaseOnly;
            mReferenceables.appendRecord(std::move(record), CSMWorld::UniversalId::Type_Door);
        }
    }
}

bool CSMWorld::Data::continueLoading(CSMDoc::Messages& messages)
{
    if (mReaderIndex == 0)
        throw std::logic_error("can't continue loading, because no load has been started");
    ESM::ReadersCache::BusyItem reader = mReaders.get(mReaderIndex - 1);
    if (!reader->isOpen())
        throw std::logic_error("can't continue loading, because no load has been started");
    reader->setEncoder(&mEncoder);
    reader->setIndex(static_cast<int>(mReaderIndex - 1));
    reader->resolveParentFileIndices(mReaders);

    if (!reader->hasMoreRecs())
    {
        mDialogue = nullptr;

        loadFallbackEntries();

        return true;
    }

    ESM::NAME n = reader->getRecName();
    reader->getRecHeader();

    bool unhandledRecord = false;

    switch (n.toInt())
    {
        case ESM::REC_GLOB:
            mGlobals.load(*reader, mBase);
            break;
        case ESM::REC_GMST:
            mGmsts.load(*reader, mBase);
            break;
        case ESM::REC_SKIL:
            mSkills.load(*reader, mBase);
            break;
        case ESM::REC_CLAS:
            mClasses.load(*reader, mBase);
            break;
        case ESM::REC_FACT:
            mFactions.load(*reader, mBase);
            break;
        case ESM::REC_RACE:
            mRaces.load(*reader, mBase);
            break;
        case ESM::REC_SOUN:
            mSounds.load(*reader, mBase);
            break;
        case ESM::REC_SCPT:
            mScripts.load(*reader, mBase);
            break;
        case ESM::REC_REGN:
            mRegions.load(*reader, mBase);
            break;
        case ESM::REC_BSGN:
            mBirthsigns.load(*reader, mBase);
            break;
        case ESM::REC_SPEL:
            mSpells.load(*reader, mBase);
            break;
        case ESM::REC_ENCH:
            mEnchantments.load(*reader, mBase);
            break;
        case ESM::REC_BODY:
            mBodyParts.load(*reader, mBase);
            break;
        case ESM::REC_SNDG:
            mSoundGens.load(*reader, mBase);
            break;
        case ESM::REC_MGEF:
            mMagicEffects.load(*reader, mBase);
            break;
        case ESM::REC_PGRD:
            mPathgrids.load(*reader, mBase);
            break;
        case ESM::REC_SSCR:
            mStartScripts.load(*reader, mBase);
            break;

        case ESM::REC_LTEX:
            mLandTextures.load(*reader, mBase);
            break;

        case ESM::REC_LAND:
            mLand.load(*reader, mBase);
            break;

        case ESM::REC_CELL:
        {
            int index = mCells.load(*reader, mBase);
            if (index < 0 || index >= mCells.getSize())
            {
                // log an error and continue loading the refs to the last loaded cell
                CSMWorld::UniversalId id(CSMWorld::UniversalId::Type_None);
                messages.add(id, "Logic error: cell index out of bounds", "", CSMDoc::Message::Severity_Error);
                index = mCells.getSize() - 1;
            }

            mRefs.load(*reader, index, mBase, mRefLoadCache[mCells.getId(index)], messages);
            break;
        }

        case ESM::REC_ACTI:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Activator);
            break;
        case ESM::REC_ALCH:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Potion);
            break;
        case ESM::REC_APPA:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Apparatus);
            break;
        case ESM::REC_ARMO:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Armor);
            break;
        case ESM::REC_BOOK:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Book);
            break;
        case ESM::REC_CLOT:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Clothing);
            break;
        case ESM::REC_CONT:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Container);
            break;
        case ESM::REC_CREA:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Creature);
            break;
        case ESM::REC_DOOR:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Door);
            break;
        case ESM::REC_INGR:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Ingredient);
            break;
        case ESM::REC_LEVC:
            mReferenceables.load(*reader, mBase, UniversalId::Type_CreatureLevelledList);
            break;
        case ESM::REC_LEVI:
            mReferenceables.load(*reader, mBase, UniversalId::Type_ItemLevelledList);
            break;
        case ESM::REC_LIGH:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Light);
            break;
        case ESM::REC_LOCK:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Lockpick);
            break;
        case ESM::REC_MISC:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Miscellaneous);
            break;
        case ESM::REC_NPC_:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Npc);
            break;
        case ESM::REC_PROB:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Probe);
            break;
        case ESM::REC_REPA:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Repair);
            break;
        case ESM::REC_STAT:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Static);
            break;
        case ESM::REC_WEAP:
            mReferenceables.load(*reader, mBase, UniversalId::Type_Weapon);
            break;

        case ESM::REC_DIAL:
        {
            ESM::Dialogue record;
            bool isDeleted = false;

            record.load(*reader, isDeleted);

            if (isDeleted)
            {
                // record vector can be shuffled around which would make pointer to record invalid
                mDialogue = nullptr;

                if (mJournals.tryDelete(record.mId))
                {
                    removeDialogueInfos(record.mId, mJournalInfoOrder, mJournalInfos);
                }
                else if (mTopics.tryDelete(record.mId))
                {
                    removeDialogueInfos(record.mId, mTopicInfoOrder, mTopicInfos);
                }
                else
                {
                    messages.add(UniversalId::Type_None,
                        "Trying to delete dialogue record " + record.mId.getRefIdString() + " which does not exist", "",
                        CSMDoc::Message::Severity_Warning);
                }
            }
            else
            {
                if (record.mType == ESM::Dialogue::Journal)
                {
                    mJournals.load(record, mBase);
                    mDialogue = &mJournals.getRecord(record.mId).get();
                }
                else
                {
                    mTopics.load(record, mBase);
                    mDialogue = &mTopics.getRecord(record.mId).get();
                }
            }

            break;
        }

        case ESM::REC_INFO:
        {
            if (!mDialogue)
            {
                messages.add(UniversalId::Type_None, "Found info record not following a dialogue record", "",
                    CSMDoc::Message::Severity_Error);

                reader->skipRecord();
                break;
            }

            if (mDialogue->mType == ESM::Dialogue::Journal)
                mJournalInfos.load(*reader, mBase, *mDialogue, mJournalInfoOrder);
            else
                mTopicInfos.load(*reader, mBase, *mDialogue, mTopicInfoOrder);

            break;
        }

        case ESM::REC_FILT:

            if (!mProject)
            {
                unhandledRecord = true;
                break;
            }

            mFilters.load(*reader, mBase);
            break;

        case ESM::REC_DBGP:

            if (!mProject)
            {
                unhandledRecord = true;
                break;
            }

            mDebugProfiles.load(*reader, mBase);
            break;

        case ESM::REC_SELG:

            if (!mProject)
            {
                unhandledRecord = true;
                break;
            }

            mSelectionGroups.load(*reader, mBase);
            break;

        default:

            unhandledRecord = true;
    }

    if (unhandledRecord)
    {
        messages.add(
            UniversalId::Type_None, "Unsupported record type: " + n.toString(), "", CSMDoc::Message::Severity_Error);

        reader->skipRecord();
    }

    return false;
}

void CSMWorld::Data::finishLoading()
{
    mTopicInfos.sort(mTopicInfoOrder);
    mJournalInfos.sort(mJournalInfoOrder);
    // Release file locks so we can actually write to the file we're editing
    mReaders.clear();
}

bool CSMWorld::Data::hasId(const std::string& id) const
{
    const ESM::RefId refId = ESM::RefId::stringRefId(id);
    return getGlobals().searchId(refId) != -1 || getGmsts().searchId(refId) != -1 || getSkills().searchId(refId) != -1
        || getClasses().searchId(refId) != -1 || getFactions().searchId(refId) != -1 || getRaces().searchId(refId) != -1
        || getSounds().searchId(refId) != -1 || getScripts().searchId(refId) != -1 || getRegions().searchId(refId) != -1
        || getBirthsigns().searchId(refId) != -1 || getSpells().searchId(refId) != -1
        || getTopics().searchId(refId) != -1 || getJournals().searchId(refId) != -1 || getCells().searchId(refId) != -1
        || getEnchantments().searchId(refId) != -1 || getBodyParts().searchId(refId) != -1
        || getSoundGens().searchId(refId) != -1 || getMagicEffects().searchId(refId) != -1
        || getReferenceables().searchId(refId) != -1;
}

int CSMWorld::Data::count(RecordBase::State state) const
{
    return count(state, mGlobals) + count(state, mGmsts) + count(state, mSkills) + count(state, mClasses)
        + count(state, mFactions) + count(state, mRaces) + count(state, mSounds) + count(state, mScripts)
        + count(state, mRegions) + count(state, mBirthsigns) + count(state, mSpells) + count(state, mCells)
        + count(state, mEnchantments) + count(state, mBodyParts) + count(state, mLand) + count(state, mLandTextures)
        + count(state, mSoundGens) + count(state, mMagicEffects) + count(state, mReferenceables)
        + count(state, mPathgrids) + count(state, mTopics) + count(state, mTopicInfos) + count(state, mJournals)
        + count(state, mJournalInfos);
}

std::vector<ESM::RefId> CSMWorld::Data::getIds(bool listDeleted) const
{
    std::vector<ESM::RefId> ids;

    appendIds(ids, mGlobals, listDeleted);
    appendIds(ids, mGmsts, listDeleted);
    appendIds(ids, mClasses, listDeleted);
    appendIds(ids, mFactions, listDeleted);
    appendIds(ids, mRaces, listDeleted);
    appendIds(ids, mSounds, listDeleted);
    appendIds(ids, mScripts, listDeleted);
    appendIds(ids, mRegions, listDeleted);
    appendIds(ids, mBirthsigns, listDeleted);
    appendIds(ids, mSpells, listDeleted);
    appendIds(ids, mTopics, listDeleted);
    appendIds(ids, mJournals, listDeleted);
    appendIds(ids, mCells, listDeleted);
    appendIds(ids, mEnchantments, listDeleted);
    appendIds(ids, mBodyParts, listDeleted);
    appendIds(ids, mSoundGens, listDeleted);
    appendIds(ids, mMagicEffects, listDeleted);
    appendIds(ids, mReferenceables, listDeleted);

    std::sort(ids.begin(), ids.end());

    return ids;
}

void CSMWorld::Data::assetsChanged()
{
    mVFS.get()->reset();
    VFS::registerArchives(mVFS.get(), Files::Collections(mDataPaths), mArchives, true, &mEncoder.getStatelessEncoder());

    const UniversalId assetTableIds[] = { UniversalId::Type_Meshes, UniversalId::Type_Icons, UniversalId::Type_Musics,
        UniversalId::Type_SoundsRes, UniversalId::Type_Textures, UniversalId::Type_Videos };

    size_t numAssetTables = sizeof(assetTableIds) / sizeof(UniversalId);

    for (size_t i = 0; i < numAssetTables; ++i)
    {
        ResourceTable* table = static_cast<ResourceTable*>(getTableModel(assetTableIds[i]));
        table->beginReset();
    }

    // Trigger recreation
    mResourcesManager.recreateResources();

    for (size_t i = 0; i < numAssetTables; ++i)
    {
        ResourceTable* table = static_cast<ResourceTable*>(getTableModel(assetTableIds[i]));
        table->endReset();
    }

    // Get rid of potentially old cached assets
    mResourceSystem->clearCache();

    emit assetTablesChanged();
}

void CSMWorld::Data::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (topLeft.column() <= 0)
        emit idListChanged();
}

void CSMWorld::Data::rowsChanged(const QModelIndex& parent, int start, int end)
{
    emit idListChanged();
}

const VFS::Manager* CSMWorld::Data::getVFS() const
{
    return mVFS.get();
}
