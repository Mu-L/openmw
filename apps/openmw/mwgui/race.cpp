#include "race.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_UString.h>

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwrender/characterpreview.hpp"
#include "../mwworld/esmstore.hpp"

#include "tooltips.hpp"

namespace
{
    int wrap(int index, int max)
    {
        if (index < 0)
            return max - 1;
        else if (index >= max)
            return 0;
        else
            return index;
    }

    bool sortRaces(const std::pair<ESM::RefId, std::string>& left, const std::pair<ESM::RefId, std::string>& right)
    {
        return left.second.compare(right.second) < 0;
    }

}

namespace MWGui
{

    RaceDialog::RaceDialog(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : WindowModal("openmw_chargen_race.layout")
        , mParent(parent)
        , mResourceSystem(resourceSystem)
        , mGenderIndex(0)
        , mFaceIndex(0)
        , mHairIndex(0)
        , mCurrentAngle(0)
        , mPreviewDirty(true)
    {
        // Centre dialog
        center();

        setText("AppearanceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu1", "Appearance"));
        getWidget(mPreviewImage, "PreviewImage");

        mPreviewImage->eventMouseWheel += MyGUI::newDelegate(this, &RaceDialog::onPreviewScroll);

        getWidget(mHeadRotate, "HeadRotate");

        mHeadRotate->setScrollRange(1000);
        mHeadRotate->setScrollPosition(500);
        mHeadRotate->setScrollViewPage(50);
        mHeadRotate->setScrollPage(50);
        mHeadRotate->setScrollWheelPage(50);
        mHeadRotate->eventScrollChangePosition += MyGUI::newDelegate(this, &RaceDialog::onHeadRotate);

        // Set up next/previous buttons
        MyGUI::Button *prevButton, *nextButton;

        setText("GenderChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu2", "Change Sex"));
        getWidget(prevButton, "PrevGenderButton");
        getWidget(nextButton, "NextGenderButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousGender);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextGender);

        setText("FaceChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu3", "Change Face"));
        getWidget(prevButton, "PrevFaceButton");
        getWidget(nextButton, "NextFaceButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousFace);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextFace);

        setText("HairChoiceT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu4", "Change Hair"));
        getWidget(prevButton, "PrevHairButton");
        getWidget(nextButton, "NextHairButton");
        prevButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectPreviousHair);
        nextButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onSelectNextHair);

        setText("RaceT", MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu5", "Race"));
        getWidget(mRaceList, "RaceList");
        mRaceList->setScrollVisible(true);
        mRaceList->eventListSelectAccept += MyGUI::newDelegate(this, &RaceDialog::onAccept);
        mRaceList->eventListChangePosition += MyGUI::newDelegate(this, &RaceDialog::onSelectRace);

        setText("SkillsT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sBonusSkillTitle", "Skill Bonus"));
        getWidget(mSkillList, "SkillList");
        setText("SpellPowerT",
            MWBase::Environment::get().getWindowManager()->getGameSettingString("sRaceMenu7", "Specials"));
        getWidget(mSpellPowerList, "SpellPowerList");

        MyGUI::Button* backButton;
        getWidget(backButton, "BackButton");
        backButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onBackClicked);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->setCaption(
            MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &RaceDialog::onOkClicked);

        updateRaces();
        updateSkills();
        updateSpellPowers();
    }

    void RaceDialog::setNextButtonShow(bool shown)
    {
        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");

        if (shown)
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sNext", {})));
        else
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
    }

    void RaceDialog::onOpen()
    {
        WindowModal::onOpen();

        updateRaces();
        updateSkills();
        updateSpellPowers();

        mPreviewImage->setRenderItemTexture(nullptr);

        mPreview.reset(nullptr);
        mPreviewTexture.reset(nullptr);

        mPreview = std::make_unique<MWRender::RaceSelectionPreview>(mParent, mResourceSystem);
        mPreview->rebuild();
        mPreview->setAngle(mCurrentAngle);

        mPreviewTexture
            = std::make_unique<MyGUIPlatform::OSGTexture>(mPreview->getTexture(), mPreview->getTextureStateSet());
        mPreviewImage->setRenderItemTexture(mPreviewTexture.get());
        mPreviewImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));

        const ESM::NPC& proto = mPreview->getPrototype();
        setRaceId(proto.mRace);
        setGender(proto.isMale() ? GM_Male : GM_Female);
        recountParts();

        for (size_t i = 0; i < mAvailableHeads.size(); ++i)
        {
            if (mAvailableHeads[i] == proto.mHead)
                mFaceIndex = i;
        }

        for (size_t i = 0; i < mAvailableHairs.size(); ++i)
        {
            if (mAvailableHairs[i] == proto.mHair)
                mHairIndex = i;
        }

        mPreviewDirty = true;

        size_t initialPos = mHeadRotate->getScrollRange() / 2 + mHeadRotate->getScrollRange() / 10;
        mHeadRotate->setScrollPosition(initialPos);
        onHeadRotate(mHeadRotate, initialPos);

        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mRaceList);
    }

    void RaceDialog::setRaceId(const ESM::RefId& raceId)
    {
        mCurrentRaceId = raceId;
        mRaceList->setIndexSelected(MyGUI::ITEM_NONE);
        size_t count = mRaceList->getItemCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (*mRaceList->getItemDataAt<ESM::RefId>(i) == raceId)
            {
                mRaceList->setIndexSelected(i);
                break;
            }
        }

        updateSkills();
        updateSpellPowers();
    }

    void RaceDialog::onClose()
    {
        WindowModal::onClose();

        mPreviewImage->setRenderItemTexture(nullptr);

        mPreviewTexture.reset(nullptr);
        mPreview.reset(nullptr);
    }

    // widget controls

    void RaceDialog::onOkClicked(MyGUI::Widget* _sender)
    {
        if (mRaceList->getIndexSelected() == MyGUI::ITEM_NONE)
            return;
        eventDone(this);
    }

    void RaceDialog::onBackClicked(MyGUI::Widget* _sender)
    {
        eventBack();
    }

    void RaceDialog::onPreviewScroll(MyGUI::Widget*, int _delta)
    {
        size_t oldPos = mHeadRotate->getScrollPosition();
        size_t maxPos = mHeadRotate->getScrollRange() - 1;
        size_t scrollPage = mHeadRotate->getScrollWheelPage();
        if (_delta < 0)
            mHeadRotate->setScrollPosition(oldPos + std::min(maxPos - oldPos, scrollPage));
        else
            mHeadRotate->setScrollPosition(oldPos - std::min(oldPos, scrollPage));

        onHeadRotate(mHeadRotate, mHeadRotate->getScrollPosition());
    }

    void RaceDialog::onHeadRotate(MyGUI::ScrollBar* scroll, size_t _position)
    {
        float angle = (float(_position) / (scroll->getScrollRange() - 1) - 0.5f) * osg::PI * 2;
        mPreview->setAngle(angle);

        mCurrentAngle = angle;
    }

    void RaceDialog::onSelectPreviousGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex - 1, 2);

        recountParts();
        updatePreview();
    }

    void RaceDialog::onSelectNextGender(MyGUI::Widget*)
    {
        mGenderIndex = wrap(mGenderIndex + 1, 2);

        recountParts();
        updatePreview();
    }

    void RaceDialog::onSelectPreviousFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex - 1, mAvailableHeads.size());
        updatePreview();
    }

    void RaceDialog::onSelectNextFace(MyGUI::Widget*)
    {
        mFaceIndex = wrap(mFaceIndex + 1, mAvailableHeads.size());
        updatePreview();
    }

    void RaceDialog::onSelectPreviousHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex - 1, mAvailableHairs.size());
        updatePreview();
    }

    void RaceDialog::onSelectNextHair(MyGUI::Widget*)
    {
        mHairIndex = wrap(mHairIndex + 1, mAvailableHairs.size());
        updatePreview();
    }

    void RaceDialog::onSelectRace(MyGUI::ListBox* _sender, size_t _index)
    {
        if (_index == MyGUI::ITEM_NONE)
            return;

        ESM::RefId& raceId = *mRaceList->getItemDataAt<ESM::RefId>(_index);
        if (mCurrentRaceId == raceId)
            return;

        mCurrentRaceId = raceId;

        recountParts();

        updatePreview();
        updateSkills();
        updateSpellPowers();
    }

    void RaceDialog::onAccept(MyGUI::ListBox* _sender, size_t _index)
    {
        onSelectRace(_sender, _index);
        if (mRaceList->getIndexSelected() == MyGUI::ITEM_NONE)
            return;
        eventDone(this);
    }

    void RaceDialog::getBodyParts(int part, std::vector<ESM::RefId>& out)
    {
        out.clear();
        const MWWorld::Store<ESM::BodyPart>& store = MWBase::Environment::get().getESMStore()->get<ESM::BodyPart>();

        for (const ESM::BodyPart& bodypart : store)
        {
            if (bodypart.mData.mFlags & ESM::BodyPart::BPF_NotPlayable)
                continue;
            if (bodypart.mData.mType != ESM::BodyPart::MT_Skin)
                continue;
            if (bodypart.mData.mPart != static_cast<ESM::BodyPart::MeshPart>(part))
                continue;
            if (mGenderIndex != (bodypart.mData.mFlags & ESM::BodyPart::BPF_Female))
                continue;
            if (ESM::isFirstPersonBodyPart(bodypart))
                continue;
            if (bodypart.mRace == mCurrentRaceId)
                out.push_back(bodypart.mId);
        }
    }

    void RaceDialog::recountParts()
    {
        getBodyParts(ESM::BodyPart::MP_Hair, mAvailableHairs);
        getBodyParts(ESM::BodyPart::MP_Head, mAvailableHeads);

        mFaceIndex = 0;
        mHairIndex = 0;
    }

    // update widget content

    void RaceDialog::updatePreview()
    {
        ESM::NPC record = mPreview->getPrototype();
        record.mRace = mCurrentRaceId;
        record.setIsMale(mGenderIndex == 0);

        if (mFaceIndex >= 0 && mFaceIndex < int(mAvailableHeads.size()))
            record.mHead = mAvailableHeads[mFaceIndex];

        if (mHairIndex >= 0 && mHairIndex < int(mAvailableHairs.size()))
            record.mHair = mAvailableHairs[mHairIndex];

        try
        {
            mPreview->setPrototype(record);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error creating preview: " << e.what();
        }
    }

    void RaceDialog::updateRaces()
    {
        mRaceList->removeAllItems();

        const MWWorld::Store<ESM::Race>& races = MWBase::Environment::get().getESMStore()->get<ESM::Race>();

        std::vector<std::pair<ESM::RefId, std::string>> items; // ID, name
        for (const ESM::Race& race : races)
        {
            bool playable = race.mData.mFlags & ESM::Race::Playable;
            if (!playable) // Only display playable races
                continue;

            items.emplace_back(race.mId, race.mName);
        }
        std::sort(items.begin(), items.end(), sortRaces);

        int index = 0;
        for (auto& item : items)
        {
            mRaceList->addItem(item.second, item.first);
            if (item.first == mCurrentRaceId)
                mRaceList->setIndexSelected(index);
            ++index;
        }
    }

    void RaceDialog::updateSkills()
    {
        for (MyGUI::Widget* widget : mSkillItems)
        {
            MyGUI::Gui::getInstance().destroyWidget(widget);
        }
        mSkillItems.clear();

        if (mCurrentRaceId.empty())
            return;

        Widgets::MWSkillPtr skillWidget;
        const int lineHeight = Settings::gui().mFontSize + 2;
        MyGUI::IntCoord coord1(0, 0, mSkillList->getWidth(), 18);

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mCurrentRaceId);
        for (const auto& bonus : race->mData.mBonus)
        {
            ESM::RefId skill = ESM::Skill::indexToRefId(bonus.mSkill);
            if (skill.empty()) // Skip unknown skill indexes
                continue;

            skillWidget = mSkillList->createWidget<Widgets::MWSkill>("MW_StatNameValue", coord1, MyGUI::Align::Default);
            skillWidget->setSkillId(skill);
            skillWidget->setSkillValue(Widgets::MWSkill::SkillValue(static_cast<float>(bonus.mBonus), 0.f));
            ToolTips::createSkillToolTip(skillWidget, skill);

            mSkillItems.push_back(skillWidget);

            coord1.top += lineHeight;
        }
    }

    void RaceDialog::updateSpellPowers()
    {
        for (MyGUI::Widget* widget : mSpellPowerItems)
        {
            MyGUI::Gui::getInstance().destroyWidget(widget);
        }
        mSpellPowerItems.clear();

        if (mCurrentRaceId.empty())
            return;

        const int lineHeight = Settings::gui().mFontSize + 2;
        MyGUI::IntCoord coord(0, 0, mSpellPowerList->getWidth(), lineHeight);

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mCurrentRaceId);

        int i = 0;
        for (const ESM::RefId& spellpower : race->mPowers.mList)
        {
            Widgets::MWSpellPtr spellPowerWidget = mSpellPowerList->createWidget<Widgets::MWSpell>(
                "MW_StatName", coord, MyGUI::Align::Default, std::string("SpellPower") + MyGUI::utility::toString(i));
            spellPowerWidget->setSpellId(spellpower);
            spellPowerWidget->setUserString("ToolTipType", "Spell");
            spellPowerWidget->setUserString("Spell", spellpower.serialize());

            mSpellPowerItems.push_back(spellPowerWidget);

            coord.top += lineHeight;
            ++i;
        }
    }

    const ESM::NPC& RaceDialog::getResult() const
    {
        return mPreview->getPrototype();
    }
}
