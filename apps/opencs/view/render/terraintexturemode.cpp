#include "terraintexturemode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>

#include <QDropEvent>
#include <QIcon>
#include <QSlider>
#include <QWidget>

#include <osg/Vec2f>
#include <osg/Vec3d>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/cellselection.hpp>
#include <apps/opencs/model/world/columnimp.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/view/render/terrainselection.hpp>
#include <apps/opencs/view/widget/scenetool.hpp>

#include <components/misc/scalableicon.hpp>
#include <components/misc/strings/conversion.hpp>

#include "../widget/scenetoolbar.hpp"
#include "../widget/scenetooltexturebrush.hpp"

#include "../../model/doc/document.hpp"
#include "../../model/prefs/state.hpp"
#include "../../model/world/commands.hpp"
#include "../../model/world/data.hpp"
#include "../../model/world/idtable.hpp"
#include "../../model/world/idtree.hpp"
#include "../../model/world/tablemimedata.hpp"
#include "../../model/world/universalid.hpp"

#include "brushdraw.hpp"
#include "editmode.hpp"
#include "mask.hpp"
#include "pagedworldspacewidget.hpp"
#include "worldspacewidget.hpp"

CSVRender::TerrainTextureMode::TerrainTextureMode(
    WorldspaceWidget* worldspaceWidget, osg::Group* parentNode, QWidget* parent)
    : EditMode(worldspaceWidget, Misc::ScalableIcon::load(":scenetoolbar/editing-terrain-texture"),
        Mask_Terrain | Mask_Reference, "Terrain texture editing", parent)
    , mBrushSize(1)
    , mBrushShape(CSVWidget::BrushShape_Point)
    , mTextureBrushScenetool(nullptr)
    , mDragMode(InteractionType_None)
    , mParentNode(parentNode)
    , mIsEditing(false)
{
}

void CSVRender::TerrainTextureMode::activate(CSVWidget::SceneToolbar* toolbar)
{
    if (!mTextureBrushScenetool)
    {
        mTextureBrushScenetool = new CSVWidget::SceneToolTextureBrush(
            toolbar, "scenetooltexturebrush", getWorldspaceWidget().getDocument());
        connect(mTextureBrushScenetool, &CSVWidget::SceneTool::clicked, mTextureBrushScenetool,
            &CSVWidget::SceneToolTextureBrush::activate);
        connect(mTextureBrushScenetool->mTextureBrushWindow, &CSVWidget::TextureBrushWindow::passBrushSize, this,
            &TerrainTextureMode::setBrushSize);
        connect(mTextureBrushScenetool->mTextureBrushWindow, &CSVWidget::TextureBrushWindow::passBrushShape, this,
            &TerrainTextureMode::setBrushShape);
        connect(mTextureBrushScenetool->mTextureBrushWindow->mSizeSliders->mBrushSizeSlider, &QSlider::valueChanged,
            this, &TerrainTextureMode::setBrushSize);
        connect(mTextureBrushScenetool, &CSVWidget::SceneToolTextureBrush::passTextureId, this,
            &TerrainTextureMode::setBrushTexture);
        connect(mTextureBrushScenetool->mTextureBrushWindow, &CSVWidget::TextureBrushWindow::passTextureId, this,
            &TerrainTextureMode::setBrushTexture);

        connect(mTextureBrushScenetool, qOverload<QDropEvent*>(&CSVWidget::SceneToolTextureBrush::passEvent), this,
            &TerrainTextureMode::handleDropEvent);
        connect(this, &TerrainTextureMode::passBrushTexture, mTextureBrushScenetool->mTextureBrushWindow,
            &CSVWidget::TextureBrushWindow::setBrushTexture);
        connect(this, &TerrainTextureMode::passBrushTexture, mTextureBrushScenetool,
            &CSVWidget::SceneToolTextureBrush::updateBrushHistory);
    }

    if (!mTerrainTextureSelection)
    {
        mTerrainTextureSelection
            = std::make_shared<TerrainSelection>(mParentNode, &getWorldspaceWidget(), TerrainSelectionType::Texture);
    }

    if (!mBrushDraw)
        mBrushDraw = std::make_unique<BrushDraw>(mParentNode, true);

    EditMode::activate(toolbar);
    toolbar->addTool(mTextureBrushScenetool);
}

void CSVRender::TerrainTextureMode::deactivate(CSVWidget::SceneToolbar* toolbar)
{
    if (mTextureBrushScenetool)
    {
        toolbar->removeTool(mTextureBrushScenetool);
        delete mTextureBrushScenetool;
        mTextureBrushScenetool = nullptr;
    }

    if (mTerrainTextureSelection)
    {
        mTerrainTextureSelection.reset();
    }

    if (mBrushDraw)
        mBrushDraw.reset();

    EditMode::deactivate(toolbar);
}

void CSVRender::TerrainTextureMode::primaryOpenPressed(const WorldspaceHitResult& hit) // Apply changes here
{
}

void CSVRender::TerrainTextureMode::primaryEditPressed(const WorldspaceHitResult& hit) // Apply changes here
{
    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& landTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Land));
    CSMWorld::IdTable& ltexTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_LandTextures));

    mCellId = getWorldspaceWidget().getCellId(hit.worldPos);

    QUndoStack& undoStack = document.getUndoStack();
    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = document.getData().getLandTextures();
    int index = landtexturesCollection.searchId(mBrushTexture);

    if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted() && hit.hit && hit.tag == nullptr)
    {
        undoStack.beginMacro("Edit texture records");
        if (allowLandTextureEditing(mCellId))
        {
            undoStack.push(new CSMWorld::TouchLandCommand(landTable, ltexTable, mCellId));
            editTerrainTextureGrid(hit);
        }
        undoStack.endMacro();
    }
}

void CSVRender::TerrainTextureMode::primarySelectPressed(const WorldspaceHitResult& hit)
{
    if (hit.hit && hit.tag == nullptr)
    {
        selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 0);
        mTerrainTextureSelection->clearTemporarySelection();
    }
}

void CSVRender::TerrainTextureMode::secondarySelectPressed(const WorldspaceHitResult& hit)
{
    if (hit.hit && hit.tag == nullptr)
    {
        selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 1);
        mTerrainTextureSelection->clearTemporarySelection();
    }
}

bool CSVRender::TerrainTextureMode::primaryEditStartDrag(const QPoint& pos)
{
    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());

    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& landTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Land));
    CSMWorld::IdTable& ltexTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_LandTextures));

    mCellId = getWorldspaceWidget().getCellId(hit.worldPos);

    QUndoStack& undoStack = document.getUndoStack();

    mDragMode = InteractionType_PrimaryEdit;

    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = document.getData().getLandTextures();
    const int index = landtexturesCollection.searchId(mBrushTexture);

    if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted() && hit.hit && hit.tag == nullptr)
    {
        undoStack.beginMacro("Edit texture records");
        mIsEditing = true;
        if (allowLandTextureEditing(mCellId))
        {
            undoStack.push(new CSMWorld::TouchLandCommand(landTable, ltexTable, mCellId));
            editTerrainTextureGrid(hit);
        }
    }

    return true;
}

bool CSVRender::TerrainTextureMode::secondaryEditStartDrag(const QPoint& pos)
{
    return false;
}

bool CSVRender::TerrainTextureMode::primarySelectStartDrag(const QPoint& pos)
{
    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
    mDragMode = InteractionType_PrimarySelect;
    if (!hit.hit || hit.tag != nullptr)
    {
        mDragMode = InteractionType_None;
        return false;
    }
    selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 0);
    return true;
}

bool CSVRender::TerrainTextureMode::secondarySelectStartDrag(const QPoint& pos)
{
    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
    mDragMode = InteractionType_SecondarySelect;
    if (!hit.hit || hit.tag != nullptr)
    {
        mDragMode = InteractionType_None;
        return false;
    }
    selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 1);
    return true;
}

void CSVRender::TerrainTextureMode::drag(const QPoint& pos, int diffX, int diffY, double speedFactor)
{
    if (mDragMode == InteractionType_PrimaryEdit)
    {
        WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
        std::string cellId = getWorldspaceWidget().getCellId(hit.worldPos);
        CSMDoc::Document& document = getWorldspaceWidget().getDocument();

        CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = document.getData().getLandTextures();
        const int index = landtexturesCollection.searchId(mBrushTexture);

        if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted() && hit.hit && hit.tag == nullptr)
        {
            editTerrainTextureGrid(hit);
        }
    }

    if (mDragMode == InteractionType_PrimarySelect)
    {
        WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
        if (hit.hit && hit.tag == nullptr)
            selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 0);
    }

    if (mDragMode == InteractionType_SecondarySelect)
    {
        WorldspaceHitResult hit = getWorldspaceWidget().mousePick(pos, getWorldspaceWidget().getInteractionMask());
        if (hit.hit && hit.tag == nullptr)
            selectTerrainTextures(CSMWorld::CellCoordinates::toTextureCoords(hit.worldPos), 1);
    }
}

void CSVRender::TerrainTextureMode::dragCompleted(const QPoint& pos)
{
    if (mDragMode == InteractionType_PrimaryEdit && mIsEditing)
    {
        CSMDoc::Document& document = getWorldspaceWidget().getDocument();
        QUndoStack& undoStack = document.getUndoStack();

        CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = document.getData().getLandTextures();
        const int index = landtexturesCollection.searchId(mBrushTexture);

        if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted())
        {
            undoStack.endMacro();
            mIsEditing = false;
        }
    }

    mTerrainTextureSelection->clearTemporarySelection();
}

void CSVRender::TerrainTextureMode::dragAborted() {}

void CSVRender::TerrainTextureMode::dragWheel(int diff, double speedFactor) {}

void CSVRender::TerrainTextureMode::handleDropEvent(QDropEvent* event)
{
    const CSMWorld::TableMimeData* mime = dynamic_cast<const CSMWorld::TableMimeData*>(event->mimeData());

    if (!mime) // May happen when non-records (e.g. plain text) are dragged and dropped
        return;

    if (mime->holdsType(CSMWorld::UniversalId::Type_LandTexture))
    {
        const std::vector<CSMWorld::UniversalId> ids = mime->getData();

        for (const CSMWorld::UniversalId& uid : ids)
        {
            mBrushTexture = ESM::RefId::stringRefId(uid.getId());
            emit passBrushTexture(mBrushTexture);
        }
    }
}

void CSVRender::TerrainTextureMode::editTerrainTextureGrid(const WorldspaceHitResult& hit)
{
    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& landTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Land));

    mCellId = getWorldspaceWidget().getCellId(hit.worldPos);
    if (allowLandTextureEditing(mCellId))
    {
    }

    std::pair<CSMWorld::CellCoordinates, bool> cellCoordinates_pair = CSMWorld::CellCoordinates::fromId(mCellId);

    int cellX = cellCoordinates_pair.first.getX();
    int cellY = cellCoordinates_pair.first.getY();

    // The coordinates of hit in mCellId
    int xHitInCell(float(((hit.worldPos.x() - (cellX * cellSize)) * landTextureSize / cellSize) - 0.25));
    int yHitInCell(float(((hit.worldPos.y() - (cellY * cellSize)) * landTextureSize / cellSize) + 0.25));
    if (xHitInCell < 0)
    {
        xHitInCell = xHitInCell + landTextureSize;
        cellX = cellX - 1;
    }
    if (yHitInCell > 15)
    {
        yHitInCell = yHitInCell - landTextureSize;
        cellY = cellY + 1;
    }

    mCellId = CSMWorld::CellCoordinates::generateId(cellX, cellY);
    if (allowLandTextureEditing(mCellId))
    {
    }

    std::string iteratedCellId;

    int textureColumn = landTable.findColumnIndex(CSMWorld::Columns::ColumnId_LandTexturesIndex);

    // All indices are offset by +1
    uint32_t brushInt = document.getData().getLandTextures().getRecord(mBrushTexture).get().mIndex + 1;

    int r = static_cast<float>(mBrushSize) / 2;

    if (mBrushShape == CSVWidget::BrushShape_Point)
    {
        CSMWorld::LandTexturesColumn::DataType newTerrain
            = landTable.data(landTable.getModelIndex(mCellId, textureColumn))
                  .value<CSMWorld::LandTexturesColumn::DataType>();

        if (allowLandTextureEditing(mCellId))
        {
            newTerrain[yHitInCell * landTextureSize + xHitInCell] = brushInt;
            pushEditToCommand(newTerrain, document, landTable, mCellId);
        }
    }

    if (mBrushShape == CSVWidget::BrushShape_Square)
    {
        int upperLeftCellX = cellX - std::floor(r / landTextureSize);
        int upperLeftCellY = cellY - std::floor(r / landTextureSize);
        if (xHitInCell - (r % landTextureSize) < 0)
            upperLeftCellX--;
        if (yHitInCell - (r % landTextureSize) < 0)
            upperLeftCellY--;

        int lowerrightCellX = cellX + std::floor(r / landTextureSize);
        int lowerrightCellY = cellY + std::floor(r / landTextureSize);
        if (xHitInCell + (r % landTextureSize) > landTextureSize - 1)
            lowerrightCellX++;
        if (yHitInCell + (r % landTextureSize) > landTextureSize - 1)
            lowerrightCellY++;

        for (int i_cell = upperLeftCellX; i_cell <= lowerrightCellX; i_cell++)
        {
            for (int j_cell = upperLeftCellY; j_cell <= lowerrightCellY; j_cell++)
            {
                iteratedCellId = CSMWorld::CellCoordinates::generateId(i_cell, j_cell);
                if (allowLandTextureEditing(iteratedCellId))
                {
                    CSMWorld::LandTexturesColumn::DataType newTerrain
                        = landTable.data(landTable.getModelIndex(iteratedCellId, textureColumn))
                              .value<CSMWorld::LandTexturesColumn::DataType>();
                    for (int i = 0; i < landTextureSize; i++)
                    {
                        for (int j = 0; j < landTextureSize; j++)
                        {

                            if (i_cell == cellX && j_cell == cellY && abs(i - xHitInCell) < r
                                && abs(j - yHitInCell) < r)
                            {
                                newTerrain[j * landTextureSize + i] = brushInt;
                            }
                            else
                            {
                                int distanceX(0);
                                int distanceY(0);
                                if (i_cell < cellX)
                                    distanceX = xHitInCell + landTextureSize * abs(i_cell - cellX) - i;
                                if (j_cell < cellY)
                                    distanceY = yHitInCell + landTextureSize * abs(j_cell - cellY) - j;
                                if (i_cell > cellX)
                                    distanceX = -xHitInCell + landTextureSize * abs(i_cell - cellX) + i;
                                if (j_cell > cellY)
                                    distanceY = -yHitInCell + landTextureSize * abs(j_cell - cellY) + j;
                                if (i_cell == cellX)
                                    distanceX = abs(i - xHitInCell);
                                if (j_cell == cellY)
                                    distanceY = abs(j - yHitInCell);
                                if (distanceX < r && distanceY < r)
                                    newTerrain[j * landTextureSize + i] = brushInt;
                            }
                        }
                    }
                    pushEditToCommand(newTerrain, document, landTable, iteratedCellId);
                }
            }
        }
    }

    if (mBrushShape == CSVWidget::BrushShape_Circle)
    {
        int upperLeftCellX = cellX - std::floor(r / landTextureSize);
        int upperLeftCellY = cellY - std::floor(r / landTextureSize);
        if (xHitInCell - (r % landTextureSize) < 0)
            upperLeftCellX--;
        if (yHitInCell - (r % landTextureSize) < 0)
            upperLeftCellY--;

        int lowerrightCellX = cellX + std::floor(r / landTextureSize);
        int lowerrightCellY = cellY + std::floor(r / landTextureSize);
        if (xHitInCell + (r % landTextureSize) > landTextureSize - 1)
            lowerrightCellX++;
        if (yHitInCell + (r % landTextureSize) > landTextureSize - 1)
            lowerrightCellY++;

        for (int i_cell = upperLeftCellX; i_cell <= lowerrightCellX; i_cell++)
        {
            for (int j_cell = upperLeftCellY; j_cell <= lowerrightCellY; j_cell++)
            {
                iteratedCellId = CSMWorld::CellCoordinates::generateId(i_cell, j_cell);
                if (allowLandTextureEditing(iteratedCellId))
                {
                    CSMWorld::LandTexturesColumn::DataType newTerrain
                        = landTable.data(landTable.getModelIndex(iteratedCellId, textureColumn))
                              .value<CSMWorld::LandTexturesColumn::DataType>();
                    for (int i = 0; i < landTextureSize; i++)
                    {
                        for (int j = 0; j < landTextureSize; j++)
                        {
                            if (i_cell == cellX && j_cell == cellY && abs(i - xHitInCell) < r
                                && abs(j - yHitInCell) < r)
                            {
                                int distanceX = abs(i - xHitInCell);
                                int distanceY = abs(j - yHitInCell);
                                float distance = std::round(sqrt(pow(distanceX, 2) + pow(distanceY, 2)));
                                float rf = static_cast<float>(mBrushSize) / 2;
                                if (distance < rf)
                                    newTerrain[j * landTextureSize + i] = brushInt;
                            }
                            else
                            {
                                int distanceX(0);
                                int distanceY(0);
                                if (i_cell < cellX)
                                    distanceX = xHitInCell + landTextureSize * abs(i_cell - cellX) - i;
                                if (j_cell < cellY)
                                    distanceY = yHitInCell + landTextureSize * abs(j_cell - cellY) - j;
                                if (i_cell > cellX)
                                    distanceX = -xHitInCell + landTextureSize * abs(i_cell - cellX) + i;
                                if (j_cell > cellY)
                                    distanceY = -yHitInCell + landTextureSize * abs(j_cell - cellY) + j;
                                if (i_cell == cellX)
                                    distanceX = abs(i - xHitInCell);
                                if (j_cell == cellY)
                                    distanceY = abs(j - yHitInCell);
                                float distance = std::round(sqrt(pow(distanceX, 2) + pow(distanceY, 2)));
                                float rf = static_cast<float>(mBrushSize) / 2;
                                if (distance < rf)
                                    newTerrain[j * landTextureSize + i] = brushInt;
                            }
                        }
                    }
                    pushEditToCommand(newTerrain, document, landTable, iteratedCellId);
                }
            }
        }
    }

    if (mBrushShape == CSVWidget::BrushShape_Custom)
    {
        CSMWorld::LandTexturesColumn::DataType newTerrain
            = landTable.data(landTable.getModelIndex(mCellId, textureColumn))
                  .value<CSMWorld::LandTexturesColumn::DataType>();

        if (allowLandTextureEditing(mCellId) && !mCustomBrushShape.empty())
        {
            for (auto const& value : mCustomBrushShape)
            {
                if (yHitInCell + value.second >= 0 && yHitInCell + value.second <= 15 && xHitInCell + value.first >= 0
                    && xHitInCell + value.first <= 15)
                {
                    newTerrain[(yHitInCell + value.second) * landTextureSize + xHitInCell + value.first] = brushInt;
                }
                else
                {
                    int cellXDifference = std::floor(1.0f * (xHitInCell + value.first) / landTextureSize);
                    int cellYDifference = std::floor(1.0f * (yHitInCell + value.second) / landTextureSize);
                    int xInOtherCell = xHitInCell + value.first - cellXDifference * landTextureSize;
                    int yInOtherCell = yHitInCell + value.second - cellYDifference * landTextureSize;

                    std::string cellId
                        = CSMWorld::CellCoordinates::generateId(cellX + cellXDifference, cellY + cellYDifference);
                    if (allowLandTextureEditing(cellId))
                    {
                        CSMWorld::LandTexturesColumn::DataType newTerrainOtherCell
                            = landTable.data(landTable.getModelIndex(cellId, textureColumn))
                                  .value<CSMWorld::LandTexturesColumn::DataType>();
                        newTerrainOtherCell[yInOtherCell * landTextureSize + xInOtherCell] = brushInt;
                        pushEditToCommand(newTerrainOtherCell, document, landTable, std::move(cellId));
                    }
                }
            }
            pushEditToCommand(newTerrain, document, landTable, mCellId);
        }
    }
}

bool CSVRender::TerrainTextureMode::isInCellSelection(int globalSelectionX, int globalSelectionY)
{
    if (CSVRender::PagedWorldspaceWidget* paged
        = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
    {
        std::pair<int, int> textureCoords = std::make_pair(globalSelectionX, globalSelectionY);
        std::string cellId = CSMWorld::CellCoordinates::textureGlobalToCellId(textureCoords);
        return paged->getCellSelection().has(CSMWorld::CellCoordinates::fromId(cellId).first);
    }
    return false;
}

void CSVRender::TerrainTextureMode::selectTerrainTextures(
    const std::pair<int, int>& texCoords, unsigned char selectMode)
{
    int r = mBrushSize / 2;
    std::vector<std::pair<int, int>> selections;

    if (mBrushShape == CSVWidget::BrushShape_Point)
    {
        if (isInCellSelection(texCoords.first, texCoords.second))
            selections.emplace_back(texCoords);
    }

    if (mBrushShape == CSVWidget::BrushShape_Square)
    {
        for (int i = -r; i <= r; i++)
        {
            for (int j = -r; j <= r; j++)
            {
                int x = i + texCoords.first;
                int y = j + texCoords.second;
                if (isInCellSelection(x, y))
                {
                    selections.emplace_back(x, y);
                }
            }
        }
    }

    if (mBrushShape == CSVWidget::BrushShape_Circle)
    {
        for (int i = -r; i <= r; i++)
        {
            for (int j = -r; j <= r; j++)
            {
                osg::Vec2f coords(i, j);
                float rf = static_cast<float>(mBrushSize) / 2;
                if (std::round(coords.length()) < rf)
                {
                    int x = i + texCoords.first;
                    int y = j + texCoords.second;
                    if (isInCellSelection(x, y))
                    {
                        selections.emplace_back(x, y);
                    }
                }
            }
        }
    }

    if (mBrushShape == CSVWidget::BrushShape_Custom)
    {
        if (!mCustomBrushShape.empty())
        {
            for (auto const& value : mCustomBrushShape)
            {
                int x = texCoords.first + value.first;
                int y = texCoords.second + value.second;
                if (isInCellSelection(x, y))
                {
                    selections.emplace_back(x, y);
                }
            }
        }
    }

    std::string selectAction;

    if (selectMode == 0)
        selectAction = CSMPrefs::get()["3D Scene Editing"]["primary-select-action"].toString();
    else
        selectAction = CSMPrefs::get()["3D Scene Editing"]["secondary-select-action"].toString();

    if (selectAction == "Select only")
        mTerrainTextureSelection->onlySelect(selections);
    else if (selectAction == "Add to selection")
        mTerrainTextureSelection->addSelect(selections);
    else if (selectAction == "Remove from selection")
        mTerrainTextureSelection->removeSelect(selections);
    else if (selectAction == "Invert selection")
        mTerrainTextureSelection->toggleSelect(selections);
}

void CSVRender::TerrainTextureMode::pushEditToCommand(CSMWorld::LandTexturesColumn::DataType& newLandGrid,
    CSMDoc::Document& document, CSMWorld::IdTable& landTable, std::string cellId)
{
    CSMWorld::IdTable& ltexTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_LandTextures));

    QVariant changedLand;
    changedLand.setValue(newLandGrid);

    QModelIndex index(
        landTable.getModelIndex(cellId, landTable.findColumnIndex(CSMWorld::Columns::ColumnId_LandTexturesIndex)));

    QUndoStack& undoStack = document.getUndoStack();
    undoStack.push(new CSMWorld::ModifyCommand(landTable, index, changedLand));
    undoStack.push(new CSMWorld::TouchLandCommand(landTable, ltexTable, cellId));
}

bool CSVRender::TerrainTextureMode::allowLandTextureEditing(const std::string& cellId)
{
    CSMDoc::Document& document = getWorldspaceWidget().getDocument();
    CSMWorld::IdTable& landTable
        = dynamic_cast<CSMWorld::IdTable&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Land));
    CSMWorld::IdTree& cellTable
        = dynamic_cast<CSMWorld::IdTree&>(*document.getData().getTableModel(CSMWorld::UniversalId::Type_Cells));

    const ESM::RefId cellRefId = ESM::RefId::stringRefId(cellId);
    const bool noCell = document.getData().getCells().searchId(cellRefId) == -1;
    const bool noLand = document.getData().getLand().searchId(cellRefId) == -1;

    if (noCell)
    {
        std::string mode = CSMPrefs::get()["3D Scene Editing"]["outside-landedit"].toString();

        // target cell does not exist
        if (mode == "Discard")
            return false;

        if (mode == "Create cell and land, then edit")
        {
            auto createCommand = std::make_unique<CSMWorld::CreateCommand>(cellTable, cellId);
            int parentIndex = cellTable.findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
            int index = cellTable.findNestedColumnIndex(parentIndex, CSMWorld::Columns::ColumnId_Interior);
            createCommand->addNestedValue(parentIndex, index, false);
            document.getUndoStack().push(createCommand.release());

            if (CSVRender::PagedWorldspaceWidget* paged
                = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
            {
                CSMWorld::CellSelection selection = paged->getCellSelection();
                selection.add(CSMWorld::CellCoordinates::fromId(cellId).first);
                paged->setCellSelection(selection);
            }
        }
    }
    else if (CSVRender::PagedWorldspaceWidget* paged
        = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(&getWorldspaceWidget()))
    {
        CSMWorld::CellSelection selection = paged->getCellSelection();
        if (!selection.has(CSMWorld::CellCoordinates::fromId(cellId).first))
        {
            // target cell exists, but is not shown
            std::string mode = CSMPrefs::get()["3D Scene Editing"]["outside-visible-landedit"].toString();

            if (mode == "Discard")
                return false;

            if (mode == "Show cell and edit")
            {
                selection.add(CSMWorld::CellCoordinates::fromId(cellId).first);
                paged->setCellSelection(selection);
            }
        }
    }

    if (noLand)
    {
        std::string mode = CSMPrefs::get()["3D Scene Editing"]["outside-landedit"].toString();

        // target cell does not exist
        if (mode == "Discard")
            return false;

        if (mode == "Create cell and land, then edit")
        {
            document.getUndoStack().push(new CSMWorld::CreateCommand(landTable, cellId));
        }
    }

    return true;
}

void CSVRender::TerrainTextureMode::dragMoveEvent(QDragMoveEvent* event) {}

void CSVRender::TerrainTextureMode::mouseMoveEvent(QMouseEvent* event)
{
    WorldspaceHitResult hit = getWorldspaceWidget().mousePick(event->position().toPoint(), getInteractionMask());
    if (hit.hit && mBrushDraw)
        mBrushDraw->update(hit.worldPos, mBrushSize, mBrushShape);
    if (!hit.hit && mBrushDraw)
        mBrushDraw->hide();
}

std::shared_ptr<CSVRender::TerrainSelection> CSVRender::TerrainTextureMode::getTerrainSelection()
{
    return mTerrainTextureSelection;
}

void CSVRender::TerrainTextureMode::setBrushSize(int brushSize)
{
    mBrushSize = brushSize;
}

void CSVRender::TerrainTextureMode::setBrushShape(CSVWidget::BrushShape brushShape)
{
    mBrushShape = brushShape;

    // Set custom brush shape
    if (mBrushShape == CSVWidget::BrushShape_Custom && !mTerrainTextureSelection->getTerrainSelection().empty())
    {
        auto terrainSelection = mTerrainTextureSelection->getTerrainSelection();
        int selectionCenterX = 0;
        int selectionCenterY = 0;
        int selectionAmount = 0;

        for (auto const& value : terrainSelection)
        {
            selectionCenterX += value.first;
            selectionCenterY += value.second;
            ++selectionAmount;
        }

        if (selectionAmount != 0)
        {
            selectionCenterX /= selectionAmount;
            selectionCenterY /= selectionAmount;
        }

        mCustomBrushShape.clear();
        for (auto const& value : terrainSelection)
            mCustomBrushShape.emplace_back(value.first - selectionCenterX, value.second - selectionCenterY);
    }
}

void CSVRender::TerrainTextureMode::setBrushTexture(ESM::RefId brushTexture)
{
    mBrushTexture = brushTexture;
}
