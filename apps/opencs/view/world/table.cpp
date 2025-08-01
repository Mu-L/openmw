#include "table.hpp"

#include <QAction>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMetaObject>
#include <QString>

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/idtableproxymodel.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/world/dragrecordtable.hpp>

#include <components/debug/debuglog.hpp>
#include <components/misc/helpviewer.hpp>
#include <components/misc/scalableicon.hpp>
#include <components/misc/strings/algorithm.hpp>

#include "../../model/doc/document.hpp"

#include "../../model/world/commanddispatcher.hpp"
#include "../../model/world/commands.hpp"
#include "../../model/world/idtable.hpp"
#include "../../model/world/idtablebase.hpp"
#include "../../model/world/infotableproxymodel.hpp"
#include "../../model/world/landtexturetableproxymodel.hpp"

#include "../../model/prefs/shortcut.hpp"
#include "../../model/prefs/state.hpp"

#include "tableeditidaction.hpp"
#include "tableheadermouseeventhandler.hpp"
#include "util.hpp"

namespace CSMFilter
{
    class Node;
}

void CSVWorld::Table::contextMenuEvent(QContextMenuEvent* event)
{
    // configure dispatcher
    mDispatcher->setSelection(getSelectedIds());

    std::vector<CSMWorld::UniversalId> extendedTypes = mDispatcher->getExtendedTypes();

    mDispatcher->setExtendedTypes(extendedTypes);

    // create context menu
    QModelIndexList selectedRows = selectionModel()->selectedRows();
    QMenu menu(this);

    ///  \todo add menu items for select all and clear selection

    int currentRow = rowAt(event->y());
    int currentColumn = columnAt(event->x());
    if (mEditIdAction->isValidIdCell(currentRow, currentColumn))
    {
        mEditIdAction->setCell(currentRow, currentColumn);
        menu.addAction(mEditIdAction);
        menu.addSeparator();
    }

    if (!mEditLock && !(mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
    {
        if (selectedRows.size() == 1)
        {
            menu.addAction(mEditAction);

            if (mCreateAction)
                menu.addAction(mCloneAction);
        }

        if (mTouchAction)
            menu.addAction(mTouchAction);

        if (mCreateAction)
            menu.addAction(mCreateAction);

        if (mDispatcher->canRevert())
        {
            menu.addAction(mRevertAction);

            if (!extendedTypes.empty())
                menu.addAction(mExtendedRevertAction);
        }

        if (mDispatcher->canDelete())
        {
            menu.addAction(mDeleteAction);

            if (!extendedTypes.empty())
                menu.addAction(mExtendedDeleteAction);
        }

        if (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_ReorderWithinTopic)
        {
            /// \todo allow reordering of multiple rows
            if (selectedRows.size() == 1)
            {
                int column = mModel->searchColumnIndex(CSMWorld::Columns::ColumnId_Topic);

                if (column == -1)
                    column = mModel->searchColumnIndex(CSMWorld::Columns::ColumnId_Journal);

                if (column != -1)
                {
                    int row = mProxyModel->mapToSource(mProxyModel->index(selectedRows.begin()->row(), 0)).row();
                    QString curData = mModel->data(mModel->index(row, column)).toString();

                    if (row > 0)
                    {
                        QString prevData = mModel->data(mModel->index(row - 1, column)).toString();
                        if (Misc::StringUtils::ciEqual(curData.toStdString(), prevData.toStdString()))
                        {
                            menu.addAction(mMoveUpAction);
                        }
                    }

                    if (row < mModel->rowCount() - 1)
                    {
                        QString nextData = mModel->data(mModel->index(row + 1, column)).toString();
                        if (Misc::StringUtils::ciEqual(curData.toStdString(), nextData.toStdString()))
                        {
                            menu.addAction(mMoveDownAction);
                        }
                    }
                }
            }
        }
    }

    if (selectedRows.size() == 1)
    {
        int row = selectedRows.begin()->row();

        row = mProxyModel->mapToSource(mProxyModel->index(row, 0)).row();

        if (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_View)
        {
            CSMWorld::UniversalId id = mModel->view(row).first;

            const int index = mDocument.getData().getCells().searchId(ESM::RefId::stringRefId(id.getId()));
            // index==-1: the ID references a worldspace instead of a cell (ignore for now and go
            // ahead)

            if (index == -1 || !mDocument.getData().getCells().getRecord(index).isDeleted())
                menu.addAction(mViewAction);
        }

        if (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Preview)
        {
            const CSMWorld::UniversalId id = getUniversalId(currentRow);
            const CSMWorld::UniversalId::Type type = id.getType();

            QModelIndex index = mModel->index(row, mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Modification));

            CSMWorld::RecordBase::State state = static_cast<CSMWorld::RecordBase::State>(mModel->data(index).toInt());

            if (state != CSMWorld::RecordBase::State_Deleted && type != CSMWorld::UniversalId::Type_ItemLevelledList)
                menu.addAction(mPreviewAction);
        }
    }

    if (mHelpAction)
        menu.addAction(mHelpAction);

    menu.exec(event->globalPos());
}

void CSVWorld::Table::mouseDoubleClickEvent(QMouseEvent* event)
{
    Qt::KeyboardModifiers modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);

    QModelIndex index = currentIndex();

    selectionModel()->select(
        index, QItemSelectionModel::Clear | QItemSelectionModel::Select | QItemSelectionModel::Rows);

    std::map<Qt::KeyboardModifiers, DoubleClickAction>::iterator iter = mDoubleClickActions.find(modifiers);

    if (iter == mDoubleClickActions.end())
    {
        event->accept();
        return;
    }

    switch (iter->second)
    {
        case Action_None:

            event->accept();
            break;

        case Action_InPlaceEdit:

            DragRecordTable::mouseDoubleClickEvent(event);
            break;

        case Action_EditRecord:

            event->accept();
            editRecord();
            break;

        case Action_View:

            event->accept();
            viewRecord();
            break;

        case Action_Revert:

            event->accept();
            if (mDispatcher->canRevert())
                mDispatcher->executeRevert();
            break;

        case Action_Delete:

            event->accept();
            if (mDispatcher->canDelete())
                mDispatcher->executeDelete();
            break;

        case Action_EditRecordAndClose:

            event->accept();
            editRecord();
            emit closeRequest();
            break;

        case Action_ViewAndClose:

            event->accept();
            viewRecord();
            emit closeRequest();
            break;
    }
}

CSVWorld::Table::Table(const CSMWorld::UniversalId& id, bool createAndDelete, bool sorting, CSMDoc::Document& document)
    : DragRecordTable(document)
    , mCreateAction(nullptr)
    , mCloneAction(nullptr)
    , mTouchAction(nullptr)
    , mRecordStatusDisplay(0)
    , mJumpToAddedRecord(false)
    , mUnselectAfterJump(false)
    , mAutoJump(false)
{
    mModel = &dynamic_cast<CSMWorld::IdTableBase&>(*mDocument.getData().getTableModel(id));

    bool isInfoTable = id.getType() == CSMWorld::UniversalId::Type_TopicInfos
        || id.getType() == CSMWorld::UniversalId::Type_JournalInfos;
    bool isLtexTable = (id.getType() == CSMWorld::UniversalId::Type_LandTextures);
    if (isInfoTable)
    {
        mProxyModel = new CSMWorld::InfoTableProxyModel(id.getType(), this);
        connect(this, &CSVWorld::DragRecordTable::moveRecordsFromSameTable, this, &CSVWorld::Table::moveRecords);
        connect(this, &CSVWorld::DragRecordTable::createNewInfoRecord, this,
            &CSVWorld::Table::createRecordsDirectlyRequest);
    }
    else if (isLtexTable)
    {
        mProxyModel = new CSMWorld::LandTextureTableProxyModel(this);
    }
    else
    {
        mProxyModel = new CSMWorld::IdTableProxyModel(this);
    }
    mProxyModel->setSourceModel(mModel);

    mDispatcher = new CSMWorld::CommandDispatcher(document, id, this);

    setModel(mProxyModel);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    // The number is arbitrary but autoresize would be way too slow otherwise.
    constexpr int autoResizePrecision = 500;
    horizontalHeader()->setResizeContentsPrecision(autoResizePrecision);
    resizeColumnsToContents();
    verticalHeader()->hide();
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    int columns = mModel->columnCount();
    for (int i = 0; i < columns; ++i)
    {
        int flags = mModel->headerData(i, Qt::Horizontal, CSMWorld::ColumnBase::Role_Flags).toInt();

        if (flags & CSMWorld::ColumnBase::Flag_Table)
        {
            CSMWorld::ColumnBase::Display display = static_cast<CSMWorld::ColumnBase::Display>(
                mModel->headerData(i, Qt::Horizontal, CSMWorld::ColumnBase::Role_Display).toInt());

            CommandDelegate* delegate
                = CommandDelegateFactoryCollection::get().makeDelegate(display, mDispatcher, document, this);

            mDelegates.push_back(delegate);
            setItemDelegateForColumn(i, delegate);
        }
        else
            hideColumn(i);
    }

    if (sorting)
    {
        // FIXME: some tables (e.g. CellRef) have this column hidden, which makes it confusing
        sortByColumn(mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Id), Qt::AscendingOrder);
    }
    setSortingEnabled(sorting);

    mEditAction = new QAction(tr("Edit Record"), this);
    connect(mEditAction, &QAction::triggered, this, &Table::editRecord);
    mEditAction->setIcon(Misc::ScalableIcon::load(":edit-edit"));
    addAction(mEditAction);
    CSMPrefs::Shortcut* editShortcut = new CSMPrefs::Shortcut("table-edit", this);
    editShortcut->associateAction(mEditAction);

    if (createAndDelete)
    {
        mCreateAction = new QAction(tr("Add Record"), this);
        connect(mCreateAction, &QAction::triggered, this, &Table::createRequest);
        mCreateAction->setIcon(Misc::ScalableIcon::load(":edit-add"));
        addAction(mCreateAction);
        CSMPrefs::Shortcut* createShortcut = new CSMPrefs::Shortcut("table-add", this);
        createShortcut->associateAction(mCreateAction);

        mCloneAction = new QAction(tr("Clone Record"), this);
        connect(mCloneAction, &QAction::triggered, this, &Table::cloneRecord);
        mCloneAction->setIcon(Misc::ScalableIcon::load(":edit-clone"));
        addAction(mCloneAction);
        CSMPrefs::Shortcut* cloneShortcut = new CSMPrefs::Shortcut("table-clone", this);
        cloneShortcut->associateAction(mCloneAction);
    }

    if (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_AllowTouch)
    {
        mTouchAction = new QAction(tr("Touch Record"), this);
        connect(mTouchAction, &QAction::triggered, this, &Table::touchRecord);
        mTouchAction->setIcon(Misc::ScalableIcon::load(":edit-touch"));
        addAction(mTouchAction);
        CSMPrefs::Shortcut* touchShortcut = new CSMPrefs::Shortcut("table-touch", this);
        touchShortcut->associateAction(mTouchAction);
    }

    mRevertAction = new QAction(tr("Revert Record"), this);
    connect(mRevertAction, &QAction::triggered, mDispatcher, &CSMWorld::CommandDispatcher::executeRevert);
    mRevertAction->setIcon(Misc::ScalableIcon::load(":edit-undo"));
    addAction(mRevertAction);
    CSMPrefs::Shortcut* revertShortcut = new CSMPrefs::Shortcut("table-revert", this);
    revertShortcut->associateAction(mRevertAction);

    mDeleteAction = new QAction(tr("Delete Record"), this);
    connect(mDeleteAction, &QAction::triggered, mDispatcher, &CSMWorld::CommandDispatcher::executeDelete);
    mDeleteAction->setIcon(Misc::ScalableIcon::load(":edit-delete"));
    addAction(mDeleteAction);
    CSMPrefs::Shortcut* deleteShortcut = new CSMPrefs::Shortcut("table-remove", this);
    deleteShortcut->associateAction(mDeleteAction);

    mMoveUpAction = new QAction(tr("Move Up"), this);
    connect(mMoveUpAction, &QAction::triggered, this, &Table::moveUpRecord);
    mMoveUpAction->setIcon(Misc::ScalableIcon::load(":record-up"));
    addAction(mMoveUpAction);
    CSMPrefs::Shortcut* moveUpShortcut = new CSMPrefs::Shortcut("table-moveup", this);
    moveUpShortcut->associateAction(mMoveUpAction);

    mMoveDownAction = new QAction(tr("Move Down"), this);
    connect(mMoveDownAction, &QAction::triggered, this, &Table::moveDownRecord);
    mMoveDownAction->setIcon(Misc::ScalableIcon::load(":record-down"));
    addAction(mMoveDownAction);
    CSMPrefs::Shortcut* moveDownShortcut = new CSMPrefs::Shortcut("table-movedown", this);
    moveDownShortcut->associateAction(mMoveDownAction);

    mViewAction = new QAction(tr("View"), this);
    connect(mViewAction, &QAction::triggered, this, &Table::viewRecord);
    mViewAction->setIcon(Misc::ScalableIcon::load(":cell"));
    addAction(mViewAction);
    CSMPrefs::Shortcut* viewShortcut = new CSMPrefs::Shortcut("table-view", this);
    viewShortcut->associateAction(mViewAction);

    mPreviewAction = new QAction(tr("Preview"), this);
    connect(mPreviewAction, &QAction::triggered, this, &Table::previewRecord);
    mPreviewAction->setIcon(Misc::ScalableIcon::load(":edit-preview"));
    addAction(mPreviewAction);
    CSMPrefs::Shortcut* previewShortcut = new CSMPrefs::Shortcut("table-preview", this);
    previewShortcut->associateAction(mPreviewAction);

    mExtendedDeleteAction = new QAction(tr("Extended Delete Record"), this);
    connect(mExtendedDeleteAction, &QAction::triggered, this, &Table::executeExtendedDelete);
    mExtendedDeleteAction->setIcon(Misc::ScalableIcon::load(":edit-delete"));
    addAction(mExtendedDeleteAction);
    CSMPrefs::Shortcut* extendedDeleteShortcut = new CSMPrefs::Shortcut("table-extendeddelete", this);
    extendedDeleteShortcut->associateAction(mExtendedDeleteAction);

    mExtendedRevertAction = new QAction(tr("Extended Revert Record"), this);
    connect(mExtendedRevertAction, &QAction::triggered, this, &Table::executeExtendedRevert);
    mExtendedRevertAction->setIcon(Misc::ScalableIcon::load(":edit-undo"));
    addAction(mExtendedRevertAction);
    CSMPrefs::Shortcut* extendedRevertShortcut = new CSMPrefs::Shortcut("table-extendedrevert", this);
    extendedRevertShortcut->associateAction(mExtendedRevertAction);

    mEditIdAction = new TableEditIdAction(*this, this);
    connect(mEditIdAction, &QAction::triggered, this, &Table::editCell);
    addAction(mEditIdAction);

    mHelpAction = new QAction(tr("Help"), this);
    connect(mHelpAction, &QAction::triggered, this, &Table::openHelp);
    mHelpAction->setIcon(Misc::ScalableIcon::load(":info"));
    addAction(mHelpAction);
    CSMPrefs::Shortcut* openHelpShortcut = new CSMPrefs::Shortcut("help", this);
    openHelpShortcut->associateAction(mHelpAction);

    connect(mProxyModel, &CSMWorld::IdTableProxyModel::rowsRemoved, this, &Table::tableSizeUpdate);

    connect(mProxyModel, &CSMWorld::IdTableProxyModel::rowAdded, this, &Table::rowAdded);

    /// \note This signal could instead be connected to a slot that filters out changes not affecting
    /// the records status column (for permanence reasons)
    connect(mProxyModel, &CSMWorld::IdTableProxyModel::dataChanged, this, &Table::dataChangedEvent);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &Table::selectionSizeUpdate);

    setAcceptDrops(true);

    mDoubleClickActions.insert(std::make_pair(Qt::NoModifier, Action_InPlaceEdit));
    mDoubleClickActions.insert(std::make_pair(Qt::ShiftModifier, Action_EditRecord));
    mDoubleClickActions.insert(std::make_pair(Qt::ControlModifier, Action_View));
    mDoubleClickActions.insert(std::make_pair(Qt::ShiftModifier | Qt::ControlModifier, Action_EditRecordAndClose));

    connect(&CSMPrefs::State::get(), &CSMPrefs::State::settingChanged, this, &Table::settingChanged);
    CSMPrefs::get()["ID Tables"].update();

    new TableHeaderMouseEventHandler(this);
}

void CSVWorld::Table::setEditLock(bool locked)
{
    for (std::vector<CommandDelegate*>::iterator iter(mDelegates.begin()); iter != mDelegates.end(); ++iter)
        (*iter)->setEditLock(locked);

    DragRecordTable::setEditLock(locked);
    mDispatcher->setEditLock(locked);
}

CSMWorld::UniversalId CSVWorld::Table::getUniversalId(int row) const
{
    row = mProxyModel->mapToSource(mProxyModel->index(row, 0)).row();

    int idColumn = mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    int typeColumn = mModel->findColumnIndex(CSMWorld::Columns::ColumnId_RecordType);

    return CSMWorld::UniversalId(
        static_cast<CSMWorld::UniversalId::Type>(mModel->data(mModel->index(row, typeColumn)).toInt()),
        mModel->data(mModel->index(row, idColumn)).toString().toUtf8().constData());
}

std::vector<std::string> CSVWorld::Table::getSelectedIds() const
{
    std::vector<std::string> ids;
    QModelIndexList selectedRows = selectionModel()->selectedRows();
    int columnIndex = mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Id);

    for (QModelIndexList::const_iterator iter(selectedRows.begin()); iter != selectedRows.end(); ++iter)
    {
        int row = mProxyModel->mapToSource(mProxyModel->index(iter->row(), 0)).row();
        ids.emplace_back(mModel->data(mModel->index(row, columnIndex)).toString().toUtf8().constData());
    }
    return ids;
}

void CSVWorld::Table::editRecord()
{
    if (!mEditLock || (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
    {
        QModelIndexList selectedRows = selectionModel()->selectedRows();

        if (selectedRows.size() == 1)
            emit editRequest(getUniversalId(selectedRows.begin()->row()), "");
    }
}

void CSVWorld::Table::cloneRecord()
{
    if (!mEditLock || (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
    {
        QModelIndexList selectedRows = selectionModel()->selectedRows();
        const CSMWorld::UniversalId& toClone = getUniversalId(selectedRows.begin()->row());
        if (selectedRows.size() == 1)
        {
            emit cloneRequest(toClone);
        }
    }
}

void CSVWorld::Table::touchRecord()
{
    if (!mEditLock && mModel->getFeatures() & CSMWorld::IdTableBase::Feature_AllowTouch)
    {
        std::vector<CSMWorld::UniversalId> touchIds;

        QModelIndexList selectedRows = selectionModel()->selectedRows();
        for (auto it = selectedRows.begin(); it != selectedRows.end(); ++it)
        {
            touchIds.push_back(getUniversalId(it->row()));
        }

        emit touchRequest(touchIds);
    }
}

void CSVWorld::Table::moveUpRecord()
{
    if (mEditLock || (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
        return;

    QModelIndexList selectedRows = selectionModel()->selectedRows();

    if (selectedRows.size() == 1)
    {
        int row2 = selectedRows.begin()->row();

        if (row2 > 0)
        {
            int row = row2 - 1;

            row = mProxyModel->mapToSource(mProxyModel->index(row, 0)).row();
            row2 = mProxyModel->mapToSource(mProxyModel->index(row2, 0)).row();

            if (row2 <= row)
                throw std::runtime_error("Inconsistent row order");

            std::vector<int> newOrder(row2 - row + 1);
            newOrder[0] = row2 - row;
            newOrder[row2 - row] = 0;
            for (int i = 1; i < row2 - row; ++i)
                newOrder[i] = i;

            mDocument.getUndoStack().push(
                new CSMWorld::ReorderRowsCommand(dynamic_cast<CSMWorld::IdTable&>(*mModel), row, newOrder));
        }
    }
}

void CSVWorld::Table::moveDownRecord()
{
    if (mEditLock || (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
        return;

    QModelIndexList selectedRows = selectionModel()->selectedRows();

    if (selectedRows.size() == 1)
    {
        int row = selectedRows.begin()->row();

        if (row < mProxyModel->rowCount() - 1)
        {
            int row2 = row + 1;

            row = mProxyModel->mapToSource(mProxyModel->index(row, 0)).row();
            row2 = mProxyModel->mapToSource(mProxyModel->index(row2, 0)).row();

            if (row2 <= row)
                throw std::runtime_error("Inconsistent row order");

            std::vector<int> newOrder(row2 - row + 1);
            newOrder[0] = row2 - row;
            newOrder[row2 - row] = 0;
            for (int i = 1; i < row2 - row; ++i)
                newOrder[i] = i;

            mDocument.getUndoStack().push(
                new CSMWorld::ReorderRowsCommand(dynamic_cast<CSMWorld::IdTable&>(*mModel), row, newOrder));
        }
    }
}

void CSVWorld::Table::moveRecords(QDropEvent* event)
{
    if (mEditLock || (mModel->getFeatures() & CSMWorld::IdTableBase::Feature_Constant))
        return;

    QModelIndex targedIndex = indexAt(event->position().toPoint());

    QModelIndexList selectedRows = selectionModel()->selectedRows();
    int targetRowRaw = targedIndex.row();
    int targetRow = mProxyModel->mapToSource(mProxyModel->index(targetRowRaw, 0)).row();
    int baseRowRaw = targedIndex.row() - 1;
    int baseRow = mProxyModel->mapToSource(mProxyModel->index(baseRowRaw, 0)).row();
    int highestDifference = 0;

    for (const auto& thisRowData : selectedRows)
    {
        int thisRow = mProxyModel->mapToSource(mProxyModel->index(thisRowData.row(), 0)).row();
        if (std::abs(targetRow - thisRow) > highestDifference)
            highestDifference = std::abs(targetRow - thisRow);
        if (thisRow - 1 < baseRow)
            baseRow = thisRow - 1;
    }

    std::vector<int> newOrder(highestDifference + 1);

    for (int i = 0; i <= highestDifference; ++i)
    {
        newOrder[i] = i;
    }

    if (selectedRows.size() > 1)
    {
        Log(Debug::Warning) << "Move operation failed: Moving multiple selections isn't implemented.";
        return;
    }

    for (const auto& thisRowData : selectedRows)
    {
        /*
            Moving algorithm description
            a) Remove the (ORIGIN + 1)th list member.
            b) Add (ORIGIN+1)th list member with value TARGET
            c) If ORIGIN > TARGET,d_INC; ELSE d_DEC
            d_INC) increase all members after (and including) the TARGET by one, stop before hitting ORIGINth address
            d_DEC)  decrease all members after the ORIGIN by one, stop after hitting address TARGET
        */

        int originRowRaw = thisRowData.row();
        int originRow = mProxyModel->mapToSource(mProxyModel->index(originRowRaw, 0)).row();

        newOrder.erase(newOrder.begin() + originRow - baseRow - 1);
        newOrder.emplace(newOrder.begin() + originRow - baseRow - 1, targetRow - baseRow - 1);

        if (originRow > targetRow)
        {
            for (int i = targetRow - baseRow - 1; i < originRow - baseRow - 1; ++i)
            {
                ++newOrder[i];
            }
        }
        else
        {
            for (int i = originRow - baseRow; i <= targetRow - baseRow - 1; ++i)
            {
                --newOrder[i];
            }
        }
    }
    mDocument.getUndoStack().push(
        new CSMWorld::ReorderRowsCommand(dynamic_cast<CSMWorld::IdTable&>(*mModel), baseRow + 1, newOrder));
}

void CSVWorld::Table::editCell()
{
    emit editRequest(mEditIdAction->getCurrentId(), "");
}

void CSVWorld::Table::openHelp()
{
    Misc::HelpViewer::openHelp("manuals/openmw-cs/tables.html");
}

void CSVWorld::Table::viewRecord()
{
    if (!(mModel->getFeatures() & CSMWorld::IdTableBase::Feature_View))
        return;

    QModelIndexList selectedRows = selectionModel()->selectedRows();

    if (selectedRows.size() == 1)
    {
        int row = selectedRows.begin()->row();

        row = mProxyModel->mapToSource(mProxyModel->index(row, 0)).row();

        std::pair<CSMWorld::UniversalId, std::string> params = mModel->view(row);

        if (params.first.getType() != CSMWorld::UniversalId::Type_None)
            emit editRequest(params.first, params.second);
    }
}

void CSVWorld::Table::previewRecord()
{
    QModelIndexList selectedRows = selectionModel()->selectedRows();

    if (selectedRows.size() == 1)
    {
        CSMWorld::UniversalId id = getUniversalId(selectedRows.begin()->row());

        QModelIndex index
            = mModel->getModelIndex(id.getId(), mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Modification));

        if (mModel->data(index) != CSMWorld::RecordBase::State_Deleted)
            emit editRequest(CSMWorld::UniversalId(CSMWorld::UniversalId::Type_Preview, id), "");
    }
}

void CSVWorld::Table::executeExtendedDelete()
{
    if (CSMPrefs::get()["ID Tables"]["extended-config"].isTrue())
    {
        emit extendedDeleteConfigRequest(getSelectedIds());
    }
    else
    {
        QMetaObject::invokeMethod(mDispatcher, "executeExtendedDelete", Qt::QueuedConnection);
    }
}

void CSVWorld::Table::executeExtendedRevert()
{
    if (CSMPrefs::get()["ID Tables"]["extended-config"].isTrue())
    {
        emit extendedRevertConfigRequest(getSelectedIds());
    }
    else
    {
        QMetaObject::invokeMethod(mDispatcher, "executeExtendedRevert", Qt::QueuedConnection);
    }
}

void CSVWorld::Table::settingChanged(const CSMPrefs::Setting* setting)
{
    if (*setting == "ID Tables/jump-to-added")
    {
        if (setting->toString() == "Jump and Select")
        {
            mJumpToAddedRecord = true;
            mUnselectAfterJump = false;
        }
        else if (setting->toString() == "Jump Only")
        {
            mJumpToAddedRecord = true;
            mUnselectAfterJump = true;
        }
        else // No Jump
        {
            mJumpToAddedRecord = false;
            mUnselectAfterJump = false;
        }
    }
    else if (*setting == "Records/type-format" || *setting == "Records/status-format")
    {
        int columns = mModel->columnCount();

        for (int i = 0; i < columns; ++i)
            if (QAbstractItemDelegate* delegate = itemDelegateForColumn(i))
            {
                dynamic_cast<CommandDelegate&>(*delegate).settingChanged(setting);
                emit dataChanged(mModel->index(0, i), mModel->index(mModel->rowCount() - 1, i));
            }
    }
    else if (setting->getParent()->getKey() == "ID Tables" && setting->getKey().substr(0, 6) == "double")
    {
        std::string modifierString = setting->getKey().substr(6);

        Qt::KeyboardModifiers modifiers;

        if (modifierString == "-s")
            modifiers = Qt::ShiftModifier;
        else if (modifierString == "-c")
            modifiers = Qt::ControlModifier;
        else if (modifierString == "-sc")
            modifiers = Qt::ShiftModifier | Qt::ControlModifier;

        DoubleClickAction action = Action_None;

        std::string value = setting->toString();

        if (value == "Edit in Place")
            action = Action_InPlaceEdit;
        else if (value == "Edit Record")
            action = Action_EditRecord;
        else if (value == "View")
            action = Action_View;
        else if (value == "Revert")
            action = Action_Revert;
        else if (value == "Delete")
            action = Action_Delete;
        else if (value == "Edit Record and Close")
            action = Action_EditRecordAndClose;
        else if (value == "View and Close")
            action = Action_ViewAndClose;

        mDoubleClickActions[modifiers] = action;
    }
}

void CSVWorld::Table::tableSizeUpdate()
{
    int size = 0;
    int deleted = 0;
    int modified = 0;

    if (mProxyModel->columnCount() > 0)
    {
        int rows = mProxyModel->rowCount();

        int columnIndex = mModel->searchColumnIndex(CSMWorld::Columns::ColumnId_Modification);

        if (columnIndex != -1)
        {
            for (int i = 0; i < rows; ++i)
            {
                QModelIndex index = mProxyModel->mapToSource(mProxyModel->index(i, 0));

                int state = mModel->data(mModel->index(index.row(), columnIndex)).toInt();

                switch (state)
                {
                    case CSMWorld::RecordBase::State_BaseOnly:
                        ++size;
                        break;
                    case CSMWorld::RecordBase::State_Modified:
                        ++size;
                        ++modified;
                        break;
                    case CSMWorld::RecordBase::State_ModifiedOnly:
                        ++size;
                        ++modified;
                        break;
                    case CSMWorld::RecordBase::State_Deleted:
                        ++deleted;
                        ++modified;
                        break;
                }
            }
        }
        else
            size = rows;
    }

    emit tableSizeChanged(size, deleted, modified);
}

void CSVWorld::Table::selectionSizeUpdate()
{
    emit selectionSizeChanged(selectionModel()->selectedRows().size());
}

void CSVWorld::Table::requestFocus(const std::string& id)
{
    QModelIndex index = mProxyModel->getModelIndex(id, 0);

    if (index.isValid())
    {
        // This will scroll to the row.
        selectRow(index.row());
        // This will actually select it.
        selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

void CSVWorld::Table::recordFilterChanged(std::shared_ptr<CSMFilter::Node> filter)
{
    mProxyModel->setFilter(filter);
    tableSizeUpdate();
    selectionSizeUpdate();
}

void CSVWorld::Table::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        startDragFromTable(*this, indexAt(event->position().toPoint()));
    }
}

std::vector<std::string> CSVWorld::Table::getColumnsWithDisplay(CSMWorld::ColumnBase::Display display) const
{
    const int count = mModel->columnCount();

    std::vector<std::string> titles;
    for (int i = 0; i < count; ++i)
    {
        CSMWorld::ColumnBase::Display columndisplay = static_cast<CSMWorld::ColumnBase::Display>(
            mModel->headerData(i, Qt::Horizontal, CSMWorld::ColumnBase::Role_Display).toInt());

        if (display == columndisplay)
        {
            titles.emplace_back(mModel->headerData(i, Qt::Horizontal).toString().toUtf8().constData());
        }
    }
    return titles;
}

std::vector<CSMWorld::UniversalId> CSVWorld::Table::getDraggedRecords() const
{
    QModelIndexList selectedRows = selectionModel()->selectedRows();
    std::vector<CSMWorld::UniversalId> idToDrag;

    for (QModelIndex& it : selectedRows)
        idToDrag.push_back(getUniversalId(it.row()));

    return idToDrag;
}

// parent, start and end depend on the model sending the signal, in this case mProxyModel
//
// If, for example, mModel was used instead, then scrolTo() should use the index
//   mProxyModel->mapFromSource(mModel->index(end, 0))
void CSVWorld::Table::rowAdded(const std::string& id)
{
    tableSizeUpdate();
    if (mJumpToAddedRecord)
    {
        int idColumn = mModel->findColumnIndex(CSMWorld::Columns::ColumnId_Id);
        int end = mProxyModel->getModelIndex(id, idColumn).row();
        selectRow(end);

        // without this delay the scroll works but goes to top for add/clone
        QMetaObject::invokeMethod(this, "queuedScrollTo", Qt::QueuedConnection, Q_ARG(int, end));

        if (mUnselectAfterJump)
            clearSelection();
    }
}

void CSVWorld::Table::queuedScrollTo(int row)
{
    scrollTo(mProxyModel->index(row, 0), QAbstractItemView::PositionAtCenter);
}

void CSVWorld::Table::dataChangedEvent(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    tableSizeUpdate();

    if (mAutoJump)
    {
        selectRow(bottomRight.row());
        scrollTo(bottomRight, QAbstractItemView::PositionAtCenter);
    }
}

void CSVWorld::Table::jumpAfterModChanged(int state)
{
    if (state == Qt::Checked)
        mAutoJump = true;
    else
        mAutoJump = false;
}
