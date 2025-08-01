#include "dragrecordtable.hpp"

#include <QDrag>
#include <QDragEnterEvent>

#include <algorithm>
#include <string>

#include "../../model/doc/document.hpp"

#include "../../model/world/commands.hpp"
#include "../../model/world/tablemimedata.hpp"

#include <apps/opencs/model/world/columnbase.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include <components/misc/scalableicon.hpp>

#include "dragdroputils.hpp"

void CSVWorld::DragRecordTable::startDragFromTable(const CSVWorld::DragRecordTable& table, const QModelIndex& index)
{
    std::vector<CSMWorld::UniversalId> records = table.getDraggedRecords();
    if (records.empty())
    {
        return;
    }

    CSMWorld::TableMimeData* mime = new CSMWorld::TableMimeData(records, mDocument);
    mime->setTableOfDragStart(&table);
    mime->setIndexAtDragStart(index);
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(Misc::ScalableIcon::load(mime->getIcon().c_str()).pixmap(QSize(16, 16)));
    drag->exec(Qt::CopyAction);
}

CSVWorld::DragRecordTable::DragRecordTable(CSMDoc::Document& document, QWidget* parent)
    : QTableView(parent)
    , mDocument(document)
    , mEditLock(false)
{
    setAcceptDrops(true);
}

void CSVWorld::DragRecordTable::setEditLock(bool locked)
{
    mEditLock = locked;
}

void CSVWorld::DragRecordTable::dragEnterEvent(QDragEnterEvent* event)
{
    event->acceptProposedAction();
}

void CSVWorld::DragRecordTable::dragMoveEvent(QDragMoveEvent* event)
{
    QModelIndex index = indexAt(event->position().toPoint());
    if (CSVWorld::DragDropUtils::canAcceptData(*event, getIndexDisplayType(index))
        || CSVWorld::DragDropUtils::isInfo(*event, getIndexDisplayType(index))
        || CSVWorld::DragDropUtils::isTopicOrJournal(*event, getIndexDisplayType(index)))
    {
        if (index.flags() & Qt::ItemIsEditable)
        {
            event->accept();
            return;
        }
    }
    event->ignore();
}

void CSVWorld::DragRecordTable::dropEvent(QDropEvent* event)
{
    QModelIndex index = indexAt(event->position().toPoint());
    CSMWorld::ColumnBase::Display display = getIndexDisplayType(index);
    if (CSVWorld::DragDropUtils::canAcceptData(*event, display))
    {
        const CSMWorld::TableMimeData* tableMimeData = CSVWorld::DragDropUtils::getTableMimeData(*event);
        if (tableMimeData->fromDocument(mDocument))
        {
            CSMWorld::UniversalId id = CSVWorld::DragDropUtils::getAcceptedData(*event, display);
            QVariant newIndexData = QString::fromUtf8(id.getId().c_str());
            QVariant oldIndexData = index.data(Qt::EditRole);
            if (newIndexData != oldIndexData)
            {
                mDocument.getUndoStack().push(new CSMWorld::ModifyCommand(*model(), index, newIndexData));
            }
        }
    }
    else if (CSVWorld::DragDropUtils::isInfo(*event, display) && event->source() == this)
    {
        emit moveRecordsFromSameTable(event);
    }
    if (CSVWorld::DragDropUtils::isTopicOrJournal(*event, display))
    {
        const CSMWorld::TableMimeData* tableMimeData = CSVWorld::DragDropUtils::getTableMimeData(*event);
        for (const auto& universalId : tableMimeData->getData())
        {
            emit createNewInfoRecord(universalId.getId());
        }
    }
}

CSMWorld::ColumnBase::Display CSVWorld::DragRecordTable::getIndexDisplayType(const QModelIndex& index) const
{
    Q_ASSERT(model() != nullptr);

    if (index.isValid())
    {
        QVariant display = model()->headerData(index.column(), Qt::Horizontal, CSMWorld::ColumnBase::Role_Display);
        if (display.isValid())
        {
            return static_cast<CSMWorld::ColumnBase::Display>(display.toInt());
        }
    }
    return CSMWorld::ColumnBase::Display_None;
}

int CSVWorld::DragRecordTable::sizeHintForColumn(int column) const
{
    // Prevent the column width from getting too long or too short
    constexpr int minWidth = 100;
    constexpr int maxWidth = 300;
    int width = QTableView::sizeHintForColumn(column);
    return std::clamp(width, minWidth, maxWidth);
}
