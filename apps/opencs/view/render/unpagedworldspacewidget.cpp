#include "unpagedworldspacewidget.hpp"

#include <sstream>

#include <components/sceneutil/util.hpp>

#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/pathgrid.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/subcellcollection.hpp>
#include <apps/opencs/view/render/cell.hpp>
#include <apps/opencs/view/render/worldspacewidget.hpp>

#include <components/esm3/loadcell.hpp>

#include <osg/Camera>
#include <osg/Vec4f>
#include <osg/ref_ptr>
#include <osgViewer/View>

#include "../../model/doc/document.hpp"

#include "../../model/world/data.hpp"
#include "../../model/world/idtable.hpp"

#include "../widget/scenetooltoggle2.hpp"

#include "cameracontroller.hpp"

namespace CSVRender
{
    class TagBase;
}

namespace osg
{
    class Vec3f;
}

void CSVRender::UnpagedWorldspaceWidget::update()
{
    const CSMWorld::Record<CSMWorld::Cell>& record
        = dynamic_cast<const CSMWorld::Record<CSMWorld::Cell>&>(mCellsModel->getRecord(mCellId));

    osg::Vec4f colour = SceneUtil::colourFromRGB(record.get().mAmbi.mAmbient);

    setDefaultAmbient(colour);

    bool isInterior = (record.get().mData.mFlags & ESM::Cell::Interior) != 0;
    bool behaveLikeExterior = (record.get().mData.mFlags & ESM::Cell::QuasiEx) != 0;

    setExterior(behaveLikeExterior || !isInterior);

    /// \todo deal with mSunlight and mFog/mForDensity

    flagAsModified();
}

CSVRender::UnpagedWorldspaceWidget::UnpagedWorldspaceWidget(
    const std::string& cellId, CSMDoc::Document& document, QWidget* parent)
    : WorldspaceWidget(document, parent)
    , mDocument(document)
    , mCellId(cellId)
{
    mCellsModel
        = &dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Cells));

    mReferenceablesModel = &dynamic_cast<CSMWorld::IdTable&>(
        *document.getData().getTableModel(CSMWorld::UniversalId::Type_Referenceables));

    connect(mCellsModel, &CSMWorld::IdTable::dataChanged, this, &UnpagedWorldspaceWidget::cellDataChanged);
    connect(mCellsModel, &CSMWorld::IdTable::rowsAboutToBeRemoved, this,
        &UnpagedWorldspaceWidget::cellRowsAboutToBeRemoved);

    connect(
        &document.getData(), &CSMWorld::Data::assetTablesChanged, this, &UnpagedWorldspaceWidget::assetTablesChanged);

    update();

    mCell = std::make_unique<Cell>(document, mSelectionMarker.get(), mRootNode, mCellId);
}

void CSVRender::UnpagedWorldspaceWidget::cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    int index = mCellsModel->findColumnIndex(CSMWorld::Columns::ColumnId_Modification);
    QModelIndex cellIndex = mCellsModel->getModelIndex(mCellId, index);

    if (cellIndex.row() >= topLeft.row() && cellIndex.row() <= bottomRight.row())
    {
        if (mCellsModel->data(cellIndex).toInt() == CSMWorld::RecordBase::State_Deleted)
        {
            emit closeRequest();
        }
        else
        {
            /// \todo possible optimisation: check columns and update only if relevant columns have
            /// changed
            update();
        }
    }
}

void CSVRender::UnpagedWorldspaceWidget::cellRowsAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    QModelIndex cellIndex = mCellsModel->getModelIndex(mCellId, 0);

    if (cellIndex.row() >= start && cellIndex.row() <= end)
        emit closeRequest();
}

void CSVRender::UnpagedWorldspaceWidget::assetTablesChanged()
{
    if (mCell)
        mCell->reloadAssets();
}

bool CSVRender::UnpagedWorldspaceWidget::handleDrop(
    const std::vector<CSMWorld::UniversalId>& universalIdData, DropType type)
{
    if (WorldspaceWidget::handleDrop(universalIdData, type))
        return true;

    if (type != Type_CellsInterior)
        return false;

    mCellId = universalIdData.begin()->getId();

    mCell = std::make_unique<Cell>(getDocument(), mSelectionMarker.get(), mRootNode, mCellId);
    mCamPositionSet = false;
    mOrbitCamControl->reset();

    update();
    emit cellChanged(*universalIdData.begin());

    return true;
}

void CSVRender::UnpagedWorldspaceWidget::clearSelection(int elementMask)
{
    mCell->setSelection(elementMask, Cell::Selection_Clear);
    flagAsModified();
    mSelectionMarker->detachMarker();
}

void CSVRender::UnpagedWorldspaceWidget::invertSelection(int elementMask)
{
    mCell->setSelection(elementMask, Cell::Selection_Invert);
    flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::selectAll(int elementMask)
{
    mCell->setSelection(elementMask, Cell::Selection_All);
    flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::selectAllWithSameParentId(int elementMask)
{
    mCell->selectAllWithSameParentId(elementMask);
    flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::selectInsideCube(
    const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode)
{
    mCell->selectInsideCube(pointA, pointB, dragMode);
}

void CSVRender::UnpagedWorldspaceWidget::selectWithinDistance(
    const osg::Vec3d& point, float distance, DragMode dragMode)
{
    mCell->selectWithinDistance(point, distance, dragMode);
}

std::string CSVRender::UnpagedWorldspaceWidget::getCellId(const osg::Vec3f& point) const
{
    return mCellId;
}

CSVRender::Cell* CSVRender::UnpagedWorldspaceWidget::getCell(const osg::Vec3d& point) const
{
    return mCell.get();
}

CSVRender::Cell* CSVRender::UnpagedWorldspaceWidget::getCell(const CSMWorld::CellCoordinates& coords) const
{
    return mCell.get();
}

osg::ref_ptr<CSVRender::TagBase> CSVRender::UnpagedWorldspaceWidget::getSnapTarget(unsigned int elementMask) const
{
    return mCell->getSnapTarget(elementMask);
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::UnpagedWorldspaceWidget::getSelection(
    unsigned int elementMask) const
{
    return mCell->getSelection(elementMask);
}

void CSVRender::UnpagedWorldspaceWidget::selectGroup(const std::vector<std::string>& group) const
{
    mCell->selectFromGroup(group);
}

void CSVRender::UnpagedWorldspaceWidget::unhideAll() const
{
    mCell->unhideAll();
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::UnpagedWorldspaceWidget::getEdited(
    unsigned int elementMask) const
{
    return mCell->getEdited(elementMask);
}

void CSVRender::UnpagedWorldspaceWidget::setSubMode(int subMode, unsigned int elementMask)
{
    mCell->setSubMode(subMode, elementMask);
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::UnpagedWorldspaceWidget::reset(unsigned int elementMask)
{
    mCell->reset(elementMask);
}

void CSVRender::UnpagedWorldspaceWidget::referenceableDataChanged(
    const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mCell.get())
        if (mCell.get()->referenceableDataChanged(topLeft, bottomRight))
            flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (mCell.get())
        if (mCell.get()->referenceableAboutToBeRemoved(parent, start, end))
            flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::referenceableAdded(const QModelIndex& parent, int start, int end)
{
    if (mCell.get())
    {
        QModelIndex topLeft = mReferenceablesModel->index(start, 0);
        QModelIndex bottomRight = mReferenceablesModel->index(end, mReferenceablesModel->columnCount());

        if (mCell.get()->referenceableDataChanged(topLeft, bottomRight))
            flagAsModified();
    }
}

void CSVRender::UnpagedWorldspaceWidget::referenceDataChanged(
    const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mCell.get())
        if (mCell.get()->referenceDataChanged(topLeft, bottomRight))
            flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (mCell.get())
        if (mCell.get()->referenceAboutToBeRemoved(parent, start, end))
            flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::referenceAdded(const QModelIndex& parent, int start, int end)
{
    if (mCell.get())
        if (mCell.get()->referenceAdded(parent, start, end))
            flagAsModified();
}

void CSVRender::UnpagedWorldspaceWidget::pathgridDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
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
        if (ESM::RefId::stringRefId(mCellId) == pathgrid.mId)
        {
            mCell->pathgridModified();
            flagAsModified();
            return;
        }
    }
}

void CSVRender::UnpagedWorldspaceWidget::pathgridAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids = mDocument.getData().getPathgrids();

    if (!parent.isValid())
    {
        // Pathgrid going to be deleted
        for (int row = start; row <= end; ++row)
        {
            const CSMWorld::Pathgrid& pathgrid = pathgrids.getRecord(row).get();
            if (ESM::RefId::stringRefId(mCellId) == pathgrid.mId)
            {
                mCell->pathgridRemoved();
                flagAsModified();
                return;
            }
        }
    }
}

void CSVRender::UnpagedWorldspaceWidget::pathgridAdded(const QModelIndex& parent, int start, int end)
{
    const CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& pathgrids = mDocument.getData().getPathgrids();

    if (!parent.isValid())
    {
        for (int row = start; row <= end; ++row)
        {
            const CSMWorld::Pathgrid& pathgrid = pathgrids.getRecord(row).get();
            if (ESM::RefId::stringRefId(mCellId) == pathgrid.mId)
            {
                mCell->pathgridModified();
                flagAsModified();
                return;
            }
        }
    }
}

void CSVRender::UnpagedWorldspaceWidget::addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool)
{
    WorldspaceWidget::addVisibilitySelectorButtons(tool);
    tool->addButton(Button_Terrain, Mask_Terrain, "Terrain", "", true);
}

std::string CSVRender::UnpagedWorldspaceWidget::getStartupInstruction()
{
    osg::Vec3d eye, center, up;
    mView->getCamera()->getViewMatrixAsLookAt(eye, center, up);
    osg::Vec3d position = eye;

    std::ostringstream stream;

    stream << "player->positionCell " << position.x() << ", " << position.y() << ", " << position.z() << ", 0, \""
           << mCellId << "\"";

    return stream.str();
}

CSVRender::WorldspaceWidget::dropRequirments CSVRender::UnpagedWorldspaceWidget::getDropRequirements(
    CSVRender::WorldspaceWidget::DropType type) const
{
    dropRequirments requirements = WorldspaceWidget::getDropRequirements(type);

    if (requirements != ignored)
        return requirements;

    switch (type)
    {
        case Type_CellsInterior:
            return canHandle;

        case Type_CellsExterior:
            return needPaged;

        default:
            return ignored;
    }
}

CSVRender::Object* CSVRender::UnpagedWorldspaceWidget::getObjectByReferenceId(const std::string& referenceId)
{
    return mCell->getObjectByReferenceId(referenceId);
}
