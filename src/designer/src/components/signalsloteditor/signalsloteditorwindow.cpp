// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "signalsloteditorwindow.h"
#include "signalsloteditor_p.h"
#include "signalsloteditor.h"
#include "signalslot_utils_p.h"

#include <iconloader_p.h>
#include <spacer_widget_p.h>
#include <qlayout_widget_p.h>

#include <QtDesigner/abstractformwindow.h>
#include <QtDesigner/abstractformeditor.h>
#include <QtDesigner/abstractformwindowmanager.h>
#include <QtDesigner/qextensionmanager.h>
#include <QtDesigner/abstractintegration.h>
#include <QtDesigner/container.h>
#include <QtDesigner/abstractmetadatabase.h>
#include <QtDesigner/abstractformwindowcursor.h>
#include <abstractdialoggui_p.h>

#include <QtWidgets/qbuttongroup.h>
#include <QtWidgets/qmenu.h>
#include <QtWidgets/qcombobox.h>
#include <QtWidgets/qapplication.h>
#include <QtWidgets/qitemdelegate.h>
#include <QtWidgets/qitemeditorfactory.h>
#include <QtWidgets/qtreeview.h>
#include <QtWidgets/qheaderview.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qtoolbutton.h>
#include <QtWidgets/qbuttongroup.h>
#include <QtWidgets/qtoolbar.h>

#include <QtGui/qaction.h>
#include <QtGui/qstandarditemmodel.h>

#include <QtCore/qabstractitemmodel.h>
#include <QtCore/qdebug.h>
#include <QtCore/qsortfilterproxymodel.h>

QT_BEGIN_NAMESPACE

// Add suitable form widgets to a list of objects for the  signal slot
// editor. Prevent special widgets from showing up there.
static void addWidgetToObjectList(const QWidget *w, QStringList &r)
{
    const QMetaObject *mo = w->metaObject();
    if (mo != &QLayoutWidget::staticMetaObject && mo != &Spacer::staticMetaObject) {
        const QString name = w->objectName().trimmed();
        if (!name.isEmpty())
            r.push_back(name);
    }
}

static QStringList objectNameList(QDesignerFormWindowInterface *form)
{
    QStringList result;

    QWidget *mainContainer = form->mainContainer();
    if (!mainContainer)
        return result;

    // Add main container container pages (QStatusBar, QWizardPages) etc.
    // to the list. Pages of containers on the form are not added, however.
    if (const QDesignerContainerExtension *c = qt_extension<QDesignerContainerExtension *>(form->core()->extensionManager(), mainContainer)) {
        const int count = c->count();
        for (int i = 0 ; i < count; i++)
            addWidgetToObjectList(c->widget(i), result);
    }

    const QDesignerFormWindowCursorInterface *cursor = form->cursor();
    const int widgetCount = cursor->widgetCount();
    for (int i = 0; i < widgetCount; ++i)
        addWidgetToObjectList(cursor->widget(i), result);

    const QDesignerMetaDataBaseInterface *mdb = form->core()->metaDataBase();

    // Add managed actions and actions with managed menus
    const auto actions = mainContainer->findChildren<QAction*>();
    for (QAction *a : actions) {
        if (!a->isSeparator()) {
            if (QMenu *menu = a->menu()) {
                if (mdb->item(menu))
                    result.push_back(menu->objectName());
            } else {
                if (mdb->item(a))
                    result.push_back(a->objectName());
            }
        }
    }

    // Add  managed buttons groups
    const auto buttonGroups = mainContainer->findChildren<QButtonGroup *>();
    for (QButtonGroup * b : buttonGroups) {
        if (mdb->item(b))
            result.append(b->objectName());
    }

    result.sort();
    return result;
}

namespace qdesigner_internal {

// ------------  ConnectionModel

ConnectionModel::ConnectionModel(QObject *parent)  :
    QAbstractItemModel(parent)
{
}

void ConnectionModel::setEditor(SignalSlotEditor *editor)
{
    if (m_editor == editor)
        return;
    beginResetModel();

    if (m_editor) {
        disconnect(m_editor.data(), &SignalSlotEditor::connectionAdded,
                   this, &ConnectionModel::connectionAdded);
        disconnect(m_editor.data(), &SignalSlotEditor::connectionRemoved,
                   this, &ConnectionModel::connectionRemoved);
        disconnect(m_editor.data(), &SignalSlotEditor::aboutToRemoveConnection,
                   this, &ConnectionModel::aboutToRemoveConnection);
        disconnect(m_editor.data(), &SignalSlotEditor::aboutToAddConnection,
                this, &ConnectionModel::aboutToAddConnection);
        disconnect(m_editor.data(), &SignalSlotEditor::connectionChanged,
                   this, &ConnectionModel::connectionChanged);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor.data(), &SignalSlotEditor::connectionAdded,
                this, &ConnectionModel::connectionAdded);
        connect(m_editor.data(), &SignalSlotEditor::connectionRemoved,
                this, &ConnectionModel::connectionRemoved);
        connect(m_editor.data(), &SignalSlotEditor::aboutToRemoveConnection,
                this, &ConnectionModel::aboutToRemoveConnection);
        connect(m_editor.data(), &SignalSlotEditor::aboutToAddConnection,
                this, &ConnectionModel::aboutToAddConnection);
        connect(m_editor.data(), &SignalSlotEditor::connectionChanged,
                this, &ConnectionModel::connectionChanged);
    }
    endResetModel();
}

QVariant ConnectionModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (orientation == Qt::Vertical || role != Qt::DisplayRole)
        return QVariant();

    static const QVariant senderTitle = tr("Sender");
    static const QVariant signalTitle = tr("Signal");
    static const QVariant receiverTitle = tr("Receiver");
    static const QVariant slotTitle = tr("Slot");

    switch (section) {
    case 0:
        return senderTitle;
    case 1:
        return signalTitle;
    case 2:
        return receiverTitle;
    case 3:
        return slotTitle;
    }
    return  QVariant();
}

QModelIndex ConnectionModel::index(int row, int column,
                                    const QModelIndex &parent) const
{
    if (parent.isValid() || !m_editor)
        return QModelIndex();
    if (row < 0 || row >= m_editor->connectionCount())
        return QModelIndex();
    return createIndex(row, column);
}

Connection *ConnectionModel::indexToConnection(const QModelIndex &index) const
{
    if (!index.isValid() || !m_editor)
        return nullptr;
    if (index.row() < 0 || index.row() >= m_editor->connectionCount())
        return nullptr;
    return m_editor->connection(index.row());
}

QModelIndex ConnectionModel::connectionToIndex(Connection *con) const
{
    Q_ASSERT(m_editor);
    return createIndex(m_editor->indexOfConnection(con), 0);
}

QModelIndex ConnectionModel::parent(const QModelIndex&) const
{
    return QModelIndex();
}

int ConnectionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_editor)
        return 0;
    return m_editor->connectionCount();
}

int ConnectionModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 4;
}

const SignalSlotConnection *ConnectionModel::connectionAt(const QModelIndex &index) const
{
    const int row = index.row();
    return m_editor != nullptr && row >= 0 && row < m_editor->connectionCount()
        ? static_cast<const SignalSlotConnection*>(m_editor->connection(row))
        : nullptr;
}

QVariant ConnectionModel::data(const QModelIndex &index, int role) const
{
    enum { deprecatedMember = 0 };

    const SignalSlotConnection *con = connectionAt(index);
    if (con == nullptr)
        return QVariant();

    // Mark deprecated slots red/italic. Not currently in use (historically for Qt 3 slots in Qt 4),
    // but may be used again in the future.
    switch (role) {
    case  Qt::ForegroundRole:
        return deprecatedMember ? QColor(Qt::red) : QVariant();
    case Qt::FontRole:
        if (deprecatedMember) {
            QFont font = QApplication::font();
            font.setItalic(true);
            return font;
        }
        return QVariant();
    case Qt::DisplayRole:
    case Qt::EditRole:
        return ConnectionModel::columnText(con, index.column());
    default:
        break;
    }

    return QVariant();
}

QString ConnectionModel::columnText(const SignalSlotConnection *con, int column)
{
    static const QString senderDefault = tr("<sender>");
    static const QString signalDefault = tr("<signal>");
    static const QString receiverDefault = tr("<receiver>");
    static const QString slotDefault = tr("<slot>");

    switch (column) {
        case 0: {
            const QString sender = con->sender();
            return sender.isEmpty() ? senderDefault : sender;
        }
        case 1: {
            const QString signalName = con->signal();
            return signalName.isEmpty() ? signalDefault : signalName;
        }
        case 2: {
            const QString receiver = con->receiver();
            return receiver.isEmpty() ? receiverDefault : receiver;
        }
        case 3: {
            const QString slotName = con->slot();
            return slotName.isEmpty() ? slotDefault : slotName;
        }
    }
    return QString();
}

bool ConnectionModel::setData(const QModelIndex &index, const QVariant &data, int)
{
    if (!index.isValid() || !m_editor)
        return false;
    if (data.metaType().id() != QMetaType::QString)
        return false;

    SignalSlotConnection *con = static_cast<SignalSlotConnection*>(m_editor->connection(index.row()));
    QDesignerFormWindowInterface *form = m_editor->formWindow();

    QString s = data.toString();
    switch (index.column()) {
        case 0:
            if (!s.isEmpty() && !objectNameList(form).contains(s))
                s.clear();
            m_editor->setSource(con, s);
            break;
        case 1:
            if (!memberFunctionListContains(form->core(), con->object(CETypes::EndPoint::Source), SignalMember, s))
                s.clear();
            m_editor->setSignal(con, s);
            break;
        case 2:
            if (!s.isEmpty() && !objectNameList(form).contains(s))
                s.clear();
            m_editor->setTarget(con, s);
            break;
        case 3:
            if (!memberFunctionListContains(form->core(), con->object(CETypes::EndPoint::Target), SlotMember, s))
                s.clear();
            m_editor->setSlot(con, s);
            break;
    }

    return true;
}

void ConnectionModel::connectionAdded(Connection*)
{
    endInsertRows();
}

void ConnectionModel::connectionRemoved(int)
{
    endRemoveRows();
}

void ConnectionModel::aboutToRemoveConnection(Connection *con)
{
    Q_ASSERT(m_editor);
    int idx = m_editor->indexOfConnection(con);
    beginRemoveRows(QModelIndex(), idx, idx);
}

void ConnectionModel::aboutToAddConnection(int idx)
{
    Q_ASSERT(m_editor);
    beginInsertRows(QModelIndex(), idx, idx);
}

Qt::ItemFlags ConnectionModel::flags(const QModelIndex&) const
{
    return Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled;
}

void ConnectionModel::connectionChanged(Connection *con)
{
    Q_ASSERT(m_editor);
    const int idx = m_editor->indexOfConnection(con);
    SignalSlotConnection *changedCon = static_cast<SignalSlotConnection*>(m_editor->connection(idx));
    SignalSlotConnection *c = nullptr;
    for (int i=0; i<m_editor->connectionCount(); ++i) {
        if (i == idx)
            continue;
        c = static_cast<SignalSlotConnection*>(m_editor->connection(i));
        if (c->sender() == changedCon->sender() && c->signal() == changedCon->signal()
            && c->receiver() == changedCon->receiver() && c->slot() == changedCon->slot()) {
            const QString message = tr("The connection already exists!<br>%1").arg(changedCon->toString());
            m_editor->formWindow()->core()->dialogGui()->message(m_editor->parentWidget(), QDesignerDialogGuiInterface::SignalSlotEditorMessage,
                                                                 QMessageBox::Warning,  tr("Signal and Slot Editor"), message, QMessageBox::Ok);
            break;
        }
    }
    emit dataChanged(createIndex(idx, 0), createIndex(idx, 3));
}

void ConnectionModel::updateAll()
{
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}
}

namespace {
// ---------------------- InlineEditorModel

class InlineEditorModel : public QStandardItemModel
{
    Q_OBJECT
public:
    enum {  TitleItem = 1 };

    InlineEditorModel(int rows, int cols, QObject *parent = nullptr);

    void addTitle(const QString &title);
    void addTextList(const QMap<QString, bool> &text_list);
    void addText(const QString &text);
    bool isTitle(int idx) const;

    int findText(const QString &text) const;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
};

InlineEditorModel::InlineEditorModel(int rows, int cols, QObject *parent)
    : QStandardItemModel(rows, cols, parent)
{
}

void InlineEditorModel::addTitle(const QString &title)
{
    const int cnt = rowCount();
    insertRows(cnt, 1);
    QModelIndex cat_idx = index(cnt, 0);
    setData(cat_idx, QString(title + QLatin1Char(':')), Qt::DisplayRole);
    setData(cat_idx, TitleItem, Qt::UserRole);
    QFont font = QApplication::font();
    font.setBold(true);
    setData(cat_idx, font, Qt::FontRole);
}

bool InlineEditorModel::isTitle(int idx) const
{
    if (idx == -1)
        return false;

    return data(index(idx, 0), Qt::UserRole).toInt() == TitleItem;
}

void InlineEditorModel::addText(const QString &text)
{
    const int cnt = rowCount();
    insertRows(cnt, 1);
    setData(index(cnt, 0), text, Qt::DisplayRole);
}

void InlineEditorModel::addTextList(const QMap<QString, bool> &text_list)
{
    int cnt = rowCount();
    insertRows(cnt, text_list.size());
    QFont font = QApplication::font();
    font.setItalic(true);
    QVariant fontVariant = QVariant::fromValue(font);
    QMap<QString, bool>::ConstIterator it = text_list.constBegin();
    const QMap<QString, bool>::ConstIterator itEnd = text_list.constEnd();
    while (it != itEnd) {
        const QModelIndex text_idx = index(cnt++, 0);
        setData(text_idx, it.key(), Qt::DisplayRole);
        if (it.value()) {
            setData(text_idx, fontVariant, Qt::FontRole);
            setData(text_idx, QColor(Qt::red), Qt::ForegroundRole);
        }
        ++it;
    }
}

Qt::ItemFlags InlineEditorModel::flags(const QModelIndex &index) const
{
    return isTitle(index.row())
        ? Qt::ItemFlags(Qt::ItemIsEnabled)
        : Qt::ItemFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
}

int InlineEditorModel::findText(const QString &text) const
{
    const int cnt = rowCount();
    for (int i = 0; i < cnt; ++i) {
        const QModelIndex idx = index(i, 0);
        if (data(idx, Qt::UserRole).toInt() == TitleItem)
            continue;
        if (data(idx, Qt::DisplayRole).toString() == text)
            return i;
    }
    return -1;
}

// ------------  InlineEditor
class InlineEditor : public QComboBox
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText USER true)
public:
    InlineEditor(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);

    void addTitle(const QString &title);
    void addText(const QString &text);
    void addTextList(const QMap<QString, bool> &text_list);

private slots:
    void checkSelection(int idx);

private:
    InlineEditorModel *m_model;
    int m_idx = -1;
};

InlineEditor::InlineEditor(QWidget *parent) :
    QComboBox(parent)
{
    setModel(m_model = new InlineEditorModel(0, 4, this));
    setFrame(false);
    m_idx = -1;
    connect(this, &QComboBox::activated,
            this, &InlineEditor::checkSelection);
}

void InlineEditor::checkSelection(int idx)
{
    if (idx == m_idx)
        return;

   if (m_model->isTitle(idx))
       setCurrentIndex(m_idx);
   else
       m_idx = idx;
}

void InlineEditor::addTitle(const QString &title)
{
    m_model->addTitle(title);
}

void InlineEditor::addTextList(const QMap<QString, bool> &text_list)
{
    m_model->addTextList(text_list);
}

void InlineEditor::addText(const QString &text)
{
    m_model->addText(text);
}

QString InlineEditor::text() const
{
    return currentText();
}

void InlineEditor::setText(const QString &text)
{
    m_idx = m_model->findText(text);
    if (m_idx == -1)
        m_idx = 0;
    setCurrentIndex(m_idx);
}

// ------------------ ConnectionDelegate

class ConnectionDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    ConnectionDelegate(QWidget *parent = nullptr);

    void setForm(QDesignerFormWindowInterface *form);

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

private slots:
    void emitCommitData();

private:
    QDesignerFormWindowInterface *m_form;
};

ConnectionDelegate::ConnectionDelegate(QWidget *parent)
    : QItemDelegate(parent)
{
    m_form = nullptr;

    static QItemEditorFactory *factory = nullptr;
    if (factory == nullptr) {
        factory = new QItemEditorFactory;
        QItemEditorCreatorBase *creator
            = new QItemEditorCreator<InlineEditor>("text");
        factory->registerEditor(QMetaType::QString, creator);
    }

    setItemEditorFactory(factory);
}

void ConnectionDelegate::setForm(QDesignerFormWindowInterface *form)
{
    m_form = form;
}

QWidget *ConnectionDelegate::createEditor(QWidget *parent,
                                                const QStyleOptionViewItem &option,
                                                const QModelIndex &index) const
{
    if (m_form == nullptr)
        return nullptr;

    QWidget *w = QItemDelegate::createEditor(parent, option, index);
    InlineEditor *inline_editor = qobject_cast<InlineEditor*>(w);
    Q_ASSERT(inline_editor != nullptr);
    const QAbstractItemModel *model = index.model();

    const QModelIndex obj_name_idx = model->index(index.row(), index.column() <= 1 ? 0 : 2);
    const QString obj_name = model->data(obj_name_idx, Qt::DisplayRole).toString();

    switch (index.column()) {
    case 0:
    case 2:  { // object names
        const QStringList &obj_name_list = objectNameList(m_form);
        QMap<QString, bool> markedNameList;
        markedNameList.insert(tr("<object>"), false);
        inline_editor->addTextList(markedNameList);
        markedNameList.clear();
        for (const QString &name : obj_name_list)
            markedNameList.insert(name, false);
        inline_editor->addTextList(markedNameList);
    }
        break;
    case 1:
    case 3: { // signals, slots
        const qdesigner_internal::MemberType type = index.column() == 1 ? qdesigner_internal::SignalMember : qdesigner_internal::SlotMember;
        const QModelIndex peer_index = model->index(index.row(), type == qdesigner_internal::SignalMember ? 3 : 1);
        const QString peer = model->data(peer_index, Qt::DisplayRole).toString();

        const qdesigner_internal::ClassesMemberFunctions class_list = qdesigner_internal::reverseClassesMemberFunctions(obj_name, type, peer, m_form);

        inline_editor->addText(type == qdesigner_internal::SignalMember ? tr("<signal>") : tr("<slot>"));
        for (const qdesigner_internal::ClassMemberFunctions &classInfo : class_list) {
            if (classInfo.m_className.isEmpty() || classInfo.m_memberList.isEmpty())
                continue;
            // Mark deprecated members by passing bool=true.
            QMap<QString, bool> markedMemberList;
            for (const QString &member : qAsConst(classInfo.m_memberList))
                markedMemberList.insert(member, false);
            inline_editor->addTitle(classInfo.m_className);
            inline_editor->addTextList(markedMemberList);
        }
    }
        break;
    default:
        break;
    }

    connect(inline_editor, &QComboBox::activated,
            this, &ConnectionDelegate::emitCommitData);

    return inline_editor;
}

void ConnectionDelegate::emitCommitData()
{
    InlineEditor *editor = qobject_cast<InlineEditor*>(sender());
    emit commitData(editor);
}

}

namespace qdesigner_internal {

/*******************************************************************************
** SignalSlotEditorWindow
*/

SignalSlotEditorWindow::SignalSlotEditorWindow(QDesignerFormEditorInterface *core,
                                                QWidget *parent)  :
    QWidget(parent),
    m_view(new QTreeView),
    m_editor(nullptr),
    m_add_button(new QToolButton),
    m_remove_button(new QToolButton),
    m_core(core),
    m_model(new ConnectionModel(this)),
    m_proxy_model(new QSortFilterProxyModel(this)),
    m_handling_selection_change(false)
{
    m_proxy_model->setSourceModel(m_model);
    m_view->setModel(m_proxy_model);
    m_view->setSortingEnabled(true);
    m_view->setItemDelegate(new ConnectionDelegate(this));
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    m_view->setRootIsDecorated(false);
    m_view->setTextElideMode (Qt::ElideMiddle);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SignalSlotEditorWindow::updateUi);
    connect(m_view->header(), &QHeaderView::sectionDoubleClicked,
            m_view, &QTreeView::resizeColumnToContents);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(0);

    QToolBar *toolBar = new QToolBar;
    toolBar->setIconSize(QSize(22, 22));
    m_add_button->setIcon(createIconSet(QStringLiteral("plus.png")));
    connect(m_add_button, &QAbstractButton::clicked, this, &SignalSlotEditorWindow::addConnection);
    toolBar->addWidget(m_add_button);

    m_remove_button->setIcon(createIconSet(QStringLiteral("minus.png")));
    connect(m_remove_button, &QAbstractButton::clicked, this, &SignalSlotEditorWindow::removeConnection);
    toolBar->addWidget(m_remove_button);

    layout->addWidget(toolBar);
    layout->addWidget(m_view);

    connect(core->formWindowManager(),
            &QDesignerFormWindowManagerInterface::activeFormWindowChanged,
            this, &SignalSlotEditorWindow::setActiveFormWindow);

    updateUi();
}

void SignalSlotEditorWindow::setActiveFormWindow(QDesignerFormWindowInterface *form)
{
    QDesignerIntegrationInterface *integration = m_core->integration();

    if (!m_editor.isNull()) {
        disconnect(m_view->selectionModel(),
                    &QItemSelectionModel::currentChanged,
                    this, &SignalSlotEditorWindow::updateEditorSelection);
        disconnect(m_editor.data(), &SignalSlotEditor::connectionSelected,
                   this, &SignalSlotEditorWindow::updateDialogSelection);
        disconnect(m_editor.data(), &SignalSlotEditor::connectionAdded,
                   this, &SignalSlotEditorWindow::resizeColumns);
        if (integration) {
            disconnect(integration, &QDesignerIntegrationInterface::objectNameChanged,
                       this, &SignalSlotEditorWindow::objectNameChanged);
        }
    }

    m_editor = form ? form->findChild<SignalSlotEditor*>() : nullptr;
    m_model->setEditor(m_editor);
    if (!m_editor.isNull()) {
        ConnectionDelegate *delegate
            = qobject_cast<ConnectionDelegate*>(m_view->itemDelegate());
        if (delegate != nullptr)
            delegate->setForm(form);

        connect(m_view->selectionModel(),
                &QItemSelectionModel::currentChanged,
                this, &SignalSlotEditorWindow::updateEditorSelection);
        connect(m_editor.data(), &SignalSlotEditor::connectionSelected,
                this, &SignalSlotEditorWindow::updateDialogSelection);
        connect(m_editor.data(), &SignalSlotEditor::connectionAdded,
                this, &SignalSlotEditorWindow::resizeColumns);
        if (integration) {
            connect(integration, &QDesignerIntegrationInterface::objectNameChanged,
                    this, &SignalSlotEditorWindow::objectNameChanged);
        }
    }

    resizeColumns();
    updateUi();
}

void SignalSlotEditorWindow::updateDialogSelection(Connection *con)
{
    if (m_handling_selection_change || m_editor == nullptr)
        return;

    QModelIndex index = m_proxy_model->mapFromSource(m_model->connectionToIndex(con));
    if (!index.isValid() || index == m_view->currentIndex())
        return;
    m_handling_selection_change = true;
    m_view->scrollTo(index, QTreeView::EnsureVisible);
    m_view->setCurrentIndex(index);
    m_handling_selection_change = false;

    updateUi();
}

void SignalSlotEditorWindow::updateEditorSelection(const QModelIndex &index)
{
    if (m_handling_selection_change || m_editor == nullptr)
        return;

    if (m_editor == nullptr)
        return;

    Connection *con = m_model->indexToConnection(m_proxy_model->mapToSource(index));
    if (m_editor->selected(con))
        return;
    m_handling_selection_change = true;
    m_editor->selectNone();
    m_editor->setSelected(con, true);
    m_handling_selection_change = false;

    updateUi();
}

void SignalSlotEditorWindow::objectNameChanged(QDesignerFormWindowInterface *, QObject *, const QString &, const QString &)
{
    if (m_editor)
        m_model->updateAll();
}

void SignalSlotEditorWindow::addConnection()
{
    if (m_editor.isNull())
        return;

    m_editor->addEmptyConnection();
    updateUi();
}

void SignalSlotEditorWindow::removeConnection()
{
    if (m_editor.isNull())
        return;

    m_editor->deleteSelected();
    updateUi();
}

void SignalSlotEditorWindow::updateUi()
{
    m_add_button->setEnabled(!m_editor.isNull());
    m_remove_button->setEnabled(!m_editor.isNull() && m_view->currentIndex().isValid());
}

void SignalSlotEditorWindow::resizeColumns()
{
    for (int c = 0, count = m_model->columnCount(); c < count; ++c)
        m_view->resizeColumnToContents(c);
}

} // namespace qdesigner_internal

QT_END_NAMESPACE

#include "signalsloteditorwindow.moc"
