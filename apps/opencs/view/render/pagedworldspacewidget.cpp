#include "pagedworldspacewidget.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/cellselection.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/pathgrid.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/subcellcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/cell.hpp>
#include <apps/opencs/view/render/instancedragmodes.hpp>
#include <apps/opencs/view/render/tagbase.hpp>
#include <apps/opencs/view/render/worldspacewidget.hpp>

#include <components/esm3/loadpgrd.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/scalableicon.hpp>

#include <osg/Camera>
#include <osg/Vec3f>
#include <osg/ref_ptr>
#include <osgViewer/View>

#include "../../model/prefs/shortcut.hpp"

#include "../../model/world/idtable.hpp"

#include "../widget/scenetoolmode.hpp"
#include "../widget/scenetooltoggle2.hpp"

#include "cellarrow.hpp"
#include "editmode.hpp"
#include "mask.hpp"
#include "terrainshapemode.hpp"
#include "terraintexturemode.hpp"

class QWidget;

namespace CSMWorld
{
    struct Cell;
}

namespace CSVWidget
{
    class SceneToolbar;
}

bool CSVRender::PagedWorldspaceWidget::adjustCells()
{
    bool modified = false;

    const CSMWorld::IdCollection<CSMWorld::Cell>& cells = mDocument.getData().getCells();

    {
        // remove/update
        std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin());

        while (iter != mCells.end())
        {
            if (!mSelection.has(iter->first))
            {
                // remove
                delete iter->second;
                mCells.erase(iter++);

                modified = true;
            }
            else
            {
                // update
                const int index = cells.searchId(ESM::RefId::stringRefId(iter->first.getId(mWorldspace)));

                bool deleted = index == -1 || cells.getRecord(index).mState == CSMWorld::RecordBase::State_Deleted;

                if (deleted != iter->second->isDeleted())
                {
                    modified = true;

                    auto cell = std::make_unique<Cell>(getDocument(), mSelectionMarker.get(), mRootNode,
                        iter->first.getId(mWorldspace), deleted, true);

                    delete iter->second;
                    iter->second = cell.release();
                }
                else if (!deleted)
                {
                    // delete state has not changed -> just update

                    // TODO check if name or region field has changed (cell marker)
                    // FIXME: config setting
                    // std::string name = cells.getRecord(index).get().mName;
                    // std::string region = cells.getRecord(index).get().mRegion;

                    modified = true;
                }

                ++iter;
            }
        }
    }

    // add
    for (CSMWorld::CellSelection::Iterator iter(mSelection.begin()); iter != mSelection.end(); ++iter)
    {
        if (mCells.find(*iter) == mCells.end())
        {
            addCellToScene(*iter);
            modified = true;
        }
    }

    if (modified)
    {
        for (std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator iter(mCells.begin()); iter != mCells.end();
             ++iter)
        {
            int mask = 0;

            for (int i = CellArrow::Direction_North; i <= CellArrow::Direction_East; i *= 2)
            {
                CSMWorld::CellCoordinates coordinates(iter->second->getCoordinates());

                switch (i)
                {
                    case CellArrow::Direction_North:
                        coordinates = coordinates.move(0, 1);
                        break;
                    case CellArrow::Direction_West:
                        coordinates = coordinates.move(-1, 0);
                        break;
                    case CellArrow::Direction_South:
                        coordinates = coordinates.move(0, -1);
                        break;
                    case CellArrow::Direction_East:
                        coordinates = coordinates.move(1, 0);
                        break;
                }

                if (!mSelection.has(coordinates))
                    mask |= i;
            }

            iter->second->setCellArrows(mask);
        }
    }

    return modified;
}

void CSVRender::PagedWorldspaceWidget::addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool)
{
    WorldspaceWidget::addVisibilitySelectorButtons(tool);
    tool->addButton(Button_Terrain, Mask_Terrain, "Terrain");
}

void CSVRender::PagedWorldspaceWidget::addEditModeSelectorButtons(CSVWidget::SceneToolMode* tool)
{
    WorldspaceWidget::addEditModeSelectorButtons(tool);

    /// \todo replace EditMode with suitable subclasses
    tool->addButton(new TerrainShapeMode(this, mRootNode, tool), "terrain-shape");
    tool->addButton(new TerrainTextureMode(this, mRootNode, tool), "terrain-texture");
    const QIcon vertexIcon = Misc::ScalableIcon::load(":scenetoolbar/editing-terrain-vertex-paint");
    const QIcon movementIcon = Misc::ScalableIcon::load(":scenetoolbar/editing-terrain-movement");
    tool->addButton(new EditMode(this, vertexIcon, Mask_Reference, "Terrain vertex paint editing"), "terrain-vertex");
    tool->addButton(new EditMode(this, movementIcon, Mask_Reference, "Terrain movement"), "terrain-move");
}

void CSVRender::PagedWorldspaceWidget::handleInteractionPress(const WorldspaceHitResult& hit, InteractionType type)
{
    if (hit.tag && hit.tag->getMask() == Mask_CellArrow)
    {
        if (CellArrowTag* cellArrowTag = dynamic_cast<CSVRender::CellArrowTag*>(hit.tag.get()))
        {
            CellArrow* arrow = cellArrowTag->getCellArrow();

            CSMWorld::CellCoordinates coordinates = arrow->getCoordinates();

            CellArrow::Direction direction = arrow->getDirection();

            int x = 0;
            int y = 0;

            switch (direction)
            {
                case CellArrow::Direction_North:
                    y = 1;
                    break;
                case CellArrow::Direction_West:
                    x = -1;
                    break;
                case CellArrow::Direction_South:
                    y = -1;
                    break;
                case CellArrow::Direction_East:
                    x = 1;
                    break;
            }

            bool modified = false;

            if (type == InteractionType_PrimarySelect)
            {
                addCellSelection(x, y);
                modified = true;
            }
            else if (type == InteractionType_SecondarySelect)
            {
                moveCellSelection(x, y);
                modified = true;
            }
            else // Primary/SecondaryEdit
            {
                CSMWorld::CellCoordinates newCoordinates = coordinates.move(x, y);

                if (mCells.find(newCoordinates) == mCells.end())
                {
                    addCellToScene(newCoordinates);
                    mSelection.add(newCoordinates);
                    modified = true;
                }

                if (type == InteractionType_SecondaryEdit)
                {
                    if (mCells.find(coordinates) != mCells.end())
                    {
                        removeCellFromScene(coordinates);
                        mSelection.remove(coordinates);
                        modified = true;
                    }
                }
            }

            if (modified)
                adjustCells();

            return;
        }
    }

    WorldspaceWidget::handleInteractionPress(hit, type);
}

void CSVRender::PagedWorldspaceWidget::referenceableDataChanged(
    const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
        if (iter->second->referenceableDataChanged(topLeft, bottomRight))
            flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
        if (iter->second->referenceableAboutToBeRemoved(parent, start, end))
            flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::referenceableAdded(const QModelIndex& parent, int start, int end)
{
    CSMWorld::IdTable& referenceables = dynamic_cast<CSMWorld::IdTable&>(
        *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_Referenceables));

    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
    {
        QModelIndex topLeft = referenceables.index(start, 0);
        QModelIndex bottomRight = referenceables.index(end, referenceables.columnCount());

        if (iter->second->referenceableDataChanged(topLeft, bottomRight))
            flagAsModified();
    }
}

void CSVRender::PagedWorldspaceWidget::referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
        if (iter->second->referenceDataChanged(topLeft, bottomRight))
            flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
        if (iter->second->referenceAboutToBeRemoved(parent, start, end))
            flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::referenceAdded(const QModelIndex& parent, int start, int end)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
        if (iter->second->referenceAdded(parent, start, end))
            flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::pathgridDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids = mDocument.getData().getPathgrids();

    int rowStart = -1;
    int rowEnd = -1;

    if (topLeft.parent().isValid())
    {
        rowStart = topLeft.parent().row();
        rowEnd = bottomRight.parent().row();
    }
    else
    {
        rowStart = topLeft.row();
        rowEnd = bottomRight.row();
    }

    for (int row = rowStart; row <= rowEnd; ++row)
    {
        const CSMWorld::Pathgrid& pathgrid = pathgrids.getRecord(row).get();
        CSMWorld::CellCoordinates coords = CSMWorld::CellCoordinates(pathgrid.mData.mX, pathgrid.mData.mY);

        std::map<CSMWorld::CellCoordinates, Cell*>::iterator searchResult = mCells.find(coords);
        if (searchResult != mCells.end())
        {
            searchResult->second->pathgridModified();
            flagAsModified();
        }
    }
}

void CSVRender::PagedWorldspaceWidget::pathgridAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids = mDocument.getData().getPathgrids();

    if (!parent.isValid())
    {
        // Pathgrid going to be deleted
        for (int row = start; row <= end; ++row)
        {
            const CSMWorld::Pathgrid& pathgrid = pathgrids.getRecord(row).get();
            CSMWorld::CellCoordinates coords = CSMWorld::CellCoordinates(pathgrid.mData.mX, pathgrid.mData.mY);

            std::map<CSMWorld::CellCoordinates, Cell*>::iterator searchResult = mCells.find(coords);
            if (searchResult != mCells.end())
            {
                searchResult->second->pathgridRemoved();
                flagAsModified();
            }
        }
    }
}

void CSVRender::PagedWorldspaceWidget::pathgridAdded(const QModelIndex& parent, int start, int end)
{
    const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids = mDocument.getData().getPathgrids();

    if (!parent.isValid())
    {
        for (int row = start; row <= end; ++row)
        {
            const CSMWorld::Pathgrid& pathgrid = pathgrids.getRecord(row).get();
            CSMWorld::CellCoordinates coords = CSMWorld::CellCoordinates(pathgrid.mData.mX, pathgrid.mData.mY);

            std::map<CSMWorld::CellCoordinates, Cell*>::iterator searchResult = mCells.find(coords);
            if (searchResult != mCells.end())
            {
                searchResult->second->pathgridModified();
                flagAsModified();
            }
        }
    }
}

void CSVRender::PagedWorldspaceWidget::landDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    for (int r = topLeft.row(); r <= bottomRight.row(); ++r)
    {
        const auto& id = mDocument.getData().getLand().getId(r);

        auto cellIt = mCells.find(CSMWorld::CellCoordinates::fromId(id.getRefIdString()).first);
        if (cellIt != mCells.end())
        {
            cellIt->second->landDataChanged(topLeft, bottomRight);
            flagAsModified();
        }
    }
}

void CSVRender::PagedWorldspaceWidget::landAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    for (int r = start; r <= end; ++r)
    {
        const auto& id = mDocument.getData().getLand().getId(r);

        auto cellIt = mCells.find(CSMWorld::CellCoordinates::fromId(id.getRefIdString()).first);
        if (cellIt != mCells.end())
        {
            cellIt->second->landAboutToBeRemoved(parent, start, end);
            flagAsModified();
        }
    }
}

void CSVRender::PagedWorldspaceWidget::landAdded(const QModelIndex& parent, int start, int end)
{
    for (int r = start; r <= end; ++r)
    {
        const auto& id = mDocument.getData().getLand().getId(r);

        auto cellIt = mCells.find(CSMWorld::CellCoordinates::fromId(id.getRefIdString()).first);
        if (cellIt != mCells.end())
        {
            cellIt->second->landAdded(parent, start, end);
            flagAsModified();
        }
    }
}

void CSVRender::PagedWorldspaceWidget::landTextureDataChanged(
    const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    for (auto cellIt : mCells)
        cellIt.second->landTextureChanged(topLeft, bottomRight);
    flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::landTextureAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    for (auto cellIt : mCells)
        cellIt.second->landTextureAboutToBeRemoved(parent, start, end);
    flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::landTextureAdded(const QModelIndex& parent, int start, int end)
{
    for (auto cellIt : mCells)
        cellIt.second->landTextureAdded(parent, start, end);
    flagAsModified();
}

std::string CSVRender::PagedWorldspaceWidget::getStartupInstruction()
{
    osg::Vec3d eye, center, up;
    mView->getCamera()->getViewMatrixAsLookAt(eye, center, up);
    osg::Vec3d position = eye;

    std::ostringstream stream;

    stream << "player->position " << position.x() << ", " << position.y() << ", " << position.z() << ", 0";

    return stream.str();
}

void CSVRender::PagedWorldspaceWidget::addCellToScene(const CSMWorld::CellCoordinates& coordinates)
{
    const CSMWorld::IdCollection<CSMWorld::Cell>& cells = mDocument.getData().getCells();

    const int index = cells.searchId(ESM::RefId::stringRefId(coordinates.getId(mWorldspace)));

    bool deleted = index == -1 || cells.getRecord(index).mState == CSMWorld::RecordBase::State_Deleted;

    auto cell = std::make_unique<Cell>(
        getDocument(), mSelectionMarker.get(), mRootNode, coordinates.getId(mWorldspace), deleted, true);
    EditMode* editMode = getEditMode();
    cell->setSubMode(editMode->getSubMode(), editMode->getInteractionMask());

    mCells.insert(std::make_pair(coordinates, cell.release()));
}

void CSVRender::PagedWorldspaceWidget::removeCellFromScene(const CSMWorld::CellCoordinates& coordinates)
{
    std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.find(coordinates);

    if (iter != mCells.end())
    {
        delete iter->second;
        mCells.erase(iter);
    }
}

void CSVRender::PagedWorldspaceWidget::addCellSelection(int x, int y)
{
    CSMWorld::CellSelection newSelection = mSelection;
    newSelection.move(x, y);

    for (CSMWorld::CellSelection::Iterator iter(newSelection.begin()); iter != newSelection.end(); ++iter)
    {
        if (mCells.find(*iter) == mCells.end())
        {
            addCellToScene(*iter);
            mSelection.add(*iter);
        }
    }
}

void CSVRender::PagedWorldspaceWidget::moveCellSelection(int x, int y)
{
    CSMWorld::CellSelection newSelection = mSelection;
    newSelection.move(x, y);

    for (CSMWorld::CellSelection::Iterator iter(mSelection.begin()); iter != mSelection.end(); ++iter)
    {
        if (!newSelection.has(*iter))
            removeCellFromScene(*iter);
    }

    for (CSMWorld::CellSelection::Iterator iter(newSelection.begin()); iter != newSelection.end(); ++iter)
    {
        if (!mSelection.has(*iter))
            addCellToScene(*iter);
    }

    mSelection = std::move(newSelection);
}

void CSVRender::PagedWorldspaceWidget::addCellToSceneFromCamera(int offsetX, int offsetY)
{
    osg::Vec3f eye, center, up;
    getCamera()->getViewMatrixAsLookAt(eye, center, up);

    int cellX = (int)std::floor(center.x() / Constants::CellSizeInUnits) + offsetX;
    int cellY = (int)std::floor(center.y() / Constants::CellSizeInUnits) + offsetY;

    CSMWorld::CellCoordinates cellCoordinates(cellX, cellY);

    if (!mSelection.has(cellCoordinates))
    {
        addCellToScene(cellCoordinates);
        mSelection.add(cellCoordinates);

        adjustCells();
    }
}

CSVRender::PagedWorldspaceWidget::PagedWorldspaceWidget(QWidget* parent, CSMDoc::Document& document)
    : WorldspaceWidget(document, parent)
    , mDocument(document)
    , mWorldspace("std::default")
    , mControlElements(nullptr)
    , mDisplayCellCoord(true)
{
    QAbstractItemModel* cells = document.getData().getTableModel(CSMWorld::UniversalId::Type_Cells);

    connect(cells, &QAbstractItemModel::dataChanged, this, &PagedWorldspaceWidget::cellDataChanged);
    connect(cells, &QAbstractItemModel::rowsRemoved, this, &PagedWorldspaceWidget::cellRemoved);
    connect(cells, &QAbstractItemModel::rowsInserted, this, &PagedWorldspaceWidget::cellAdded);

    connect(&document.getData(), &CSMWorld::Data::assetTablesChanged, this, &PagedWorldspaceWidget::assetTablesChanged);

    QAbstractItemModel* lands = document.getData().getTableModel(CSMWorld::UniversalId::Type_Lands);

    connect(lands, &QAbstractItemModel::dataChanged, this, &PagedWorldspaceWidget::landDataChanged);
    connect(lands, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PagedWorldspaceWidget::landAboutToBeRemoved);
    connect(lands, &QAbstractItemModel::rowsInserted, this, &PagedWorldspaceWidget::landAdded);

    QAbstractItemModel* ltexs = document.getData().getTableModel(CSMWorld::UniversalId::Type_LandTextures);

    connect(ltexs, &QAbstractItemModel::dataChanged, this, &PagedWorldspaceWidget::landTextureDataChanged);
    connect(
        ltexs, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PagedWorldspaceWidget::landTextureAboutToBeRemoved);
    connect(ltexs, &QAbstractItemModel::rowsInserted, this, &PagedWorldspaceWidget::landTextureAdded);

    // Shortcuts
    CSMPrefs::Shortcut* loadCameraCellShortcut = new CSMPrefs::Shortcut("scene-load-cam-cell", this);
    connect(loadCameraCellShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &PagedWorldspaceWidget::loadCameraCell);

    CSMPrefs::Shortcut* loadCameraEastCellShortcut = new CSMPrefs::Shortcut("scene-load-cam-eastcell", this);
    connect(loadCameraEastCellShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &PagedWorldspaceWidget::loadEastCell);

    CSMPrefs::Shortcut* loadCameraNorthCellShortcut = new CSMPrefs::Shortcut("scene-load-cam-northcell", this);
    connect(loadCameraNorthCellShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &PagedWorldspaceWidget::loadNorthCell);

    CSMPrefs::Shortcut* loadCameraWestCellShortcut = new CSMPrefs::Shortcut("scene-load-cam-westcell", this);
    connect(loadCameraWestCellShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &PagedWorldspaceWidget::loadWestCell);

    CSMPrefs::Shortcut* loadCameraSouthCellShortcut = new CSMPrefs::Shortcut("scene-load-cam-southcell", this);
    connect(loadCameraSouthCellShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this,
        &PagedWorldspaceWidget::loadSouthCell);
}

CSVRender::PagedWorldspaceWidget::~PagedWorldspaceWidget()
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter(mCells.begin()); iter != mCells.end(); ++iter)
    {
        delete iter->second;
    }
}

void CSVRender::PagedWorldspaceWidget::useViewHint(const std::string& hint)
{
    if (!hint.empty())
    {
        CSMWorld::CellSelection selection;

        if (hint[0] == 'c')
        {
            // syntax: c:#x1 y1; #x2 y2 (number of coordinate pairs can be 0 or larger)
            char ignore;

            std::istringstream stream(hint.c_str());
            if (stream >> ignore)
            {
                char ignore1; // : or ;
                char ignore2; // #
                // Current coordinate
                int x, y;

                // Loop through all the coordinates to add them to selection
                while (stream >> ignore1 >> ignore2 >> x >> y)
                    selection.add(CSMWorld::CellCoordinates(x, y));

                // Mark that camera needs setup
                mCamPositionSet = false;
            }
        }
        else if (hint[0] == 'r')
        {
            // syntax r:ref#number (e.g. r:ref#100)
            char ignore;

            std::istringstream stream(hint.c_str());
            if (stream >> ignore) // ignore r
            {
                char ignore1; // : or ;

                std::string refCode; // ref#number (e.g. ref#100)

                while (stream >> ignore1 >> refCode)
                {
                }

                // Find out cell coordinate
                CSMWorld::IdTable& references = dynamic_cast<CSMWorld::IdTable&>(
                    *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_References));
                int cellColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
                QVariant cell = references.data(references.getModelIndex(refCode, cellColumn)).value<QVariant>();
                QString cellqs = cell.toString();
                std::istringstream streamCellCoord(cellqs.toStdString().c_str());

                if (streamCellCoord >> ignore) // ignore #
                {
                    // Current coordinate
                    int x, y;

                    // Loop through all the coordinates to add them to selection
                    while (streamCellCoord >> x >> y)
                        selection.add(CSMWorld::CellCoordinates(x, y));

                    // Mark that camera needs setup
                    mCamPositionSet = false;
                }
            }
        }

        setCellSelection(selection);
    }
}

void CSVRender::PagedWorldspaceWidget::setCellSelection(const CSMWorld::CellSelection& selection)
{
    mSelection = selection;

    if (adjustCells())
        flagAsModified();

    emit cellSelectionChanged(mSelection);
}

const CSMWorld::CellSelection& CSVRender::PagedWorldspaceWidget::getCellSelection() const
{
    return mSelection;
}

std::pair<int, int> CSVRender::PagedWorldspaceWidget::getCoordinatesFromId(const std::string& record) const
{
    std::istringstream stream(record.c_str());
    char ignore;
    int x, y;
    stream >> ignore >> x >> y;
    return std::make_pair(x, y);
}

bool CSVRender::PagedWorldspaceWidget::handleDrop(
    const std::vector<CSMWorld::UniversalId>& universalIdData, DropType type)
{
    if (WorldspaceWidget::handleDrop(universalIdData, type))
        return true;

    if (type != Type_CellsExterior)
        return false;

    bool selectionChanged = false;
    for (const auto& id : universalIdData)
    {
        std::pair<int, int> coordinates(getCoordinatesFromId(id.getId()));
        if (mSelection.add(CSMWorld::CellCoordinates(coordinates.first, coordinates.second)))
        {
            selectionChanged = true;
        }
    }
    if (selectionChanged)
    {
        if (adjustCells())
            flagAsModified();

        emit cellSelectionChanged(mSelection);
    }

    return true;
}

CSVRender::WorldspaceWidget::dropRequirments CSVRender::PagedWorldspaceWidget::getDropRequirements(
    CSVRender::WorldspaceWidget::DropType type) const
{
    dropRequirments requirements = WorldspaceWidget::getDropRequirements(type);

    if (requirements != ignored)
        return requirements;

    switch (type)
    {
        case Type_CellsExterior:
            return canHandle;

        case Type_CellsInterior:
            return needUnpaged;

        default:
            return ignored;
    }
}

unsigned int CSVRender::PagedWorldspaceWidget::getVisibilityMask() const
{
    return WorldspaceWidget::getVisibilityMask() | mControlElements->getSelectionMask();
}

void CSVRender::PagedWorldspaceWidget::clearSelection(int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->setSelection(elementMask, Cell::Selection_Clear);

    flagAsModified();
    mSelectionMarker->detachMarker();
}

void CSVRender::PagedWorldspaceWidget::invertSelection(int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->setSelection(elementMask, Cell::Selection_Invert);

    flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::selectAll(int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->setSelection(elementMask, Cell::Selection_All);

    flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::selectAllWithSameParentId(int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->selectAllWithSameParentId(elementMask);

    flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::selectInsideCube(
    const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode)
{
    for (auto& cell : mCells)
    {
        cell.second->selectInsideCube(pointA, pointB, dragMode);
    }
}

void CSVRender::PagedWorldspaceWidget::selectWithinDistance(const osg::Vec3d& point, float distance, DragMode dragMode)
{
    for (auto& cell : mCells)
    {
        cell.second->selectWithinDistance(point, distance, dragMode);
    }
}

std::string CSVRender::PagedWorldspaceWidget::getCellId(const osg::Vec3f& point) const
{
    CSMWorld::CellCoordinates cellCoordinates(static_cast<int>(std::floor(point.x() / Constants::CellSizeInUnits)),
        static_cast<int>(std::floor(point.y() / Constants::CellSizeInUnits)));

    return cellCoordinates.getId(mWorldspace);
}

CSVRender::Cell* CSVRender::PagedWorldspaceWidget::getCell(const osg::Vec3d& point) const
{
    CSMWorld::CellCoordinates coords(static_cast<int>(std::floor(point.x() / Constants::CellSizeInUnits)),
        static_cast<int>(std::floor(point.y() / Constants::CellSizeInUnits)));

    std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator searchResult = mCells.find(coords);
    if (searchResult != mCells.end())
        return searchResult->second;
    else
        return nullptr;
}

CSVRender::Cell* CSVRender::PagedWorldspaceWidget::getCell(const CSMWorld::CellCoordinates& coords) const
{
    std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator searchResult = mCells.find(coords);
    if (searchResult != mCells.end())
        return searchResult->second;
    else
        return nullptr;
}

void CSVRender::PagedWorldspaceWidget::setCellAlteredHeight(
    const CSMWorld::CellCoordinates& coords, int inCellX, int inCellY, float height)
{
    std::map<CSMWorld::CellCoordinates, Cell*>::iterator searchResult = mCells.find(coords);
    if (searchResult != mCells.end())
        searchResult->second->setAlteredHeight(inCellX, inCellY, height);
}

float* CSVRender::PagedWorldspaceWidget::getCellAlteredHeight(
    const CSMWorld::CellCoordinates& coords, int inCellX, int inCellY)
{
    std::map<CSMWorld::CellCoordinates, Cell*>::iterator searchResult = mCells.find(coords);
    if (searchResult != mCells.end())
        return searchResult->second->getAlteredHeight(inCellX, inCellY);
    return nullptr;
}

void CSVRender::PagedWorldspaceWidget::resetAllAlteredHeights()
{
    for (const auto& cell : mCells)
        cell.second->resetAlteredHeights();
}

osg::ref_ptr<CSVRender::TagBase> CSVRender::PagedWorldspaceWidget::getSnapTarget(unsigned int elementMask) const
{
    osg::ref_ptr<CSVRender::TagBase> result;

    for (auto& [coords, cell] : mCells)
    {
        auto snapTarget = cell->getSnapTarget(elementMask);
        if (snapTarget)
        {
            return snapTarget;
        }
    }

    return result;
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::PagedWorldspaceWidget::getSelection(
    unsigned int elementMask) const
{
    std::vector<osg::ref_ptr<CSVRender::TagBase>> result;

    for (std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
    {
        std::vector<osg::ref_ptr<CSVRender::TagBase>> cellResult = iter->second->getSelection(elementMask);

        result.insert(result.end(), cellResult.begin(), cellResult.end());
    }

    return result;
}

void CSVRender::PagedWorldspaceWidget::selectGroup(const std::vector<std::string>& group) const
{
    for (const auto& [_, cell] : mCells)
        cell->selectFromGroup(group);
}

void CSVRender::PagedWorldspaceWidget::unhideAll() const
{
    for (const auto& [_, cell] : mCells)
        cell->unhideAll();
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::PagedWorldspaceWidget::getEdited(
    unsigned int elementMask) const
{
    std::vector<osg::ref_ptr<CSVRender::TagBase>> result;

    for (std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
    {
        std::vector<osg::ref_ptr<CSVRender::TagBase>> cellResult = iter->second->getEdited(elementMask);

        result.insert(result.end(), cellResult.begin(), cellResult.end());
    }

    return result;
}

void CSVRender::PagedWorldspaceWidget::setSubMode(int subMode, unsigned int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->setSubMode(subMode, elementMask);
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::PagedWorldspaceWidget::reset(unsigned int elementMask)
{
    for (std::map<CSMWorld::CellCoordinates, Cell*>::const_iterator iter = mCells.begin(); iter != mCells.end(); ++iter)
        iter->second->reset(elementMask);
}

CSVWidget::SceneToolToggle2* CSVRender::PagedWorldspaceWidget::makeControlVisibilitySelector(
    CSVWidget::SceneToolbar* parent)
{
    mControlElements = new CSVWidget::SceneToolToggle2(parent, "Controls & Guides Visibility",
        ":scenetoolbar/scene-view-marker-c", ":scenetoolbar/scene-view-marker-");

    mControlElements->addButton(1, Mask_CellMarker, "Cell Marker");
    mControlElements->addButton(2, Mask_CellArrow, "Cell Arrows");
    mControlElements->addButton(4, Mask_CellBorder, "Cell Border");

    mControlElements->setSelectionMask(0xffffffff);

    connect(mControlElements, &CSVWidget::SceneToolToggle2::selectionChanged, this,
        &PagedWorldspaceWidget::elementSelectionChanged);

    return mControlElements;
}

void CSVRender::PagedWorldspaceWidget::cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    /// \todo check if no selected cell is affected and do not update, if that is the case
    if (adjustCells())
        flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::cellRemoved(const QModelIndex& parent, int start, int end)
{
    if (adjustCells())
        flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::cellAdded(const QModelIndex& index, int start, int end)
{
    /// \todo check if no selected cell is affected and do not update, if that is the case
    if (adjustCells())
        flagAsModified();
}

void CSVRender::PagedWorldspaceWidget::assetTablesChanged()
{
    std::map<CSMWorld::CellCoordinates, Cell*>::iterator iter = mCells.begin();
    for (; iter != mCells.end(); ++iter)
    {
        iter->second->reloadAssets();
    }
}

void CSVRender::PagedWorldspaceWidget::loadCameraCell()
{
    addCellToSceneFromCamera(0, 0);
}

void CSVRender::PagedWorldspaceWidget::loadEastCell()
{
    addCellToSceneFromCamera(1, 0);
}

void CSVRender::PagedWorldspaceWidget::loadNorthCell()
{
    addCellToSceneFromCamera(0, 1);
}

void CSVRender::PagedWorldspaceWidget::loadWestCell()
{
    addCellToSceneFromCamera(-1, 0);
}

void CSVRender::PagedWorldspaceWidget::loadSouthCell()
{
    addCellToSceneFromCamera(0, -1);
}

CSVRender::Object* CSVRender::PagedWorldspaceWidget::getObjectByReferenceId(const std::string& referenceId)
{
    for (const auto& [_, cell] : mCells)
        if (const auto& object = cell->getObjectByReferenceId(referenceId))
            return object;

    return nullptr;
}
