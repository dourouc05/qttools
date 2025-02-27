// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "buddyeditor.h"

#include <QtDesigner/abstractformwindow.h>
#include <QtDesigner/propertysheet.h>
#include <QtDesigner/abstractformeditor.h>
#include <QtDesigner/qextensionmanager.h>

#include <qdesigner_command_p.h>
#include <qdesigner_propertycommand_p.h>
#include <qdesigner_utils_p.h>
#include <qlayout_widget_p.h>
#include <connectionedit_p.h>
#include <metadatabase_p.h>

#include <QtWidgets/qlabel.h>
#include <QtWidgets/qmenu.h>
#include <QtWidgets/qapplication.h>

#include <QtGui/qaction.h>

#include <QtCore/qdebug.h>
#include <QtCore/qlist.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

static const char *buddyPropertyC = "buddy";

static bool canBeBuddy(QWidget *w, QDesignerFormWindowInterface *form)
{
    if (qobject_cast<const QLayoutWidget*>(w) || qobject_cast<const QLabel*>(w))
        return false;
    if (w == form->mainContainer() || w->isHidden() )
        return false;

    QExtensionManager *ext = form->core()->extensionManager();
    if (QDesignerPropertySheetExtension *sheet = qt_extension<QDesignerPropertySheetExtension*>(ext, w)) {
        const int index = sheet->indexOf(QStringLiteral("focusPolicy"));
        if (index != -1) {
            bool ok = false;
            const Qt::FocusPolicy q = static_cast<Qt::FocusPolicy>(qdesigner_internal::Utils::valueOf(sheet->property(index), &ok));
            // Refuse No-focus unless the widget is promoted.
            return (ok && q != Qt::NoFocus) || qdesigner_internal::isPromoted(form->core(), w);
        }
    }
    return false;
}

static QString buddy(QLabel *label, QDesignerFormEditorInterface *core)
{
    QDesignerPropertySheetExtension *sheet = qt_extension<QDesignerPropertySheetExtension*>(core->extensionManager(), label);
    if (sheet == nullptr)
        return QString();
    const int prop_idx = sheet->indexOf(QLatin1String(buddyPropertyC));
    if (prop_idx == -1)
        return QString();
    return sheet->property(prop_idx).toString();
}

namespace qdesigner_internal {

/*******************************************************************************
** BuddyEditor
*/

BuddyEditor::BuddyEditor(QDesignerFormWindowInterface *form, QWidget *parent) :
    ConnectionEdit(parent, form),
    m_formWindow(form),
    m_updating(false)
{
}


QWidget *BuddyEditor::widgetAt(const QPoint &pos) const
{
    QWidget *w = ConnectionEdit::widgetAt(pos);

    while (w != nullptr && !m_formWindow->isManaged(w))
        w = w->parentWidget();
    if (!w)
        return w;

    if (state() == Editing) {
        QLabel *label = qobject_cast<QLabel*>(w);
        if (label == nullptr)
            return nullptr;
        const int cnt = connectionCount();
        for (int i = 0; i < cnt; ++i) {
            Connection *con = connection(i);
            if (con->widget(EndPoint::Source) == w)
                return nullptr;
        }
    } else {
        if (!canBeBuddy(w, m_formWindow))
            return nullptr;
    }

    return w;
}

Connection *BuddyEditor::createConnection(QWidget *source, QWidget *destination)
{
    return new Connection(this, source, destination);
}

QDesignerFormWindowInterface *BuddyEditor::formWindow() const
{
    return m_formWindow;
}

void BuddyEditor::updateBackground()
{
    if (m_updating || background() == nullptr)
        return;
    ConnectionEdit::updateBackground();

    m_updating = true;
    QList<Connection *> newList;
    const auto label_list = background()->findChildren<QLabel*>();
    for (QLabel *label : label_list) {
        const QString buddy_name = buddy(label, m_formWindow->core());
        if (buddy_name.isEmpty())
            continue;

        const QWidgetList targets = background()->findChildren<QWidget*>(buddy_name);
        if (targets.isEmpty())
            continue;

        const auto wit = std::find_if(targets.cbegin(), targets.cend(),
                                      [] (const QWidget *w) { return !w->isHidden(); });
        if (wit == targets.cend())
            continue;

        Connection *con = new Connection(this);
        con->setEndPoint(EndPoint::Source, label, widgetRect(label).center());
        con->setEndPoint(EndPoint::Target, *wit, widgetRect(*wit).center());
        newList.append(con);
    }

    QList<Connection *> toRemove;

    const int c = connectionCount();
    for (int i = 0; i < c; i++) {
        Connection *con = connection(i);
        QObject *source = con->object(EndPoint::Source);
        QObject *target = con->object(EndPoint::Target);
        const bool found =
            std::any_of(newList.cbegin(), newList.cend(),
                        [source, target] (const Connection *nc)
                        { return nc->object(EndPoint::Source) == source && nc->object(EndPoint::Target) == target; });
        if (!found)
            toRemove.append(con);
    }
    if (!toRemove.isEmpty()) {
        DeleteConnectionsCommand command(this, toRemove);
        command.redo();
        for (Connection *con : qAsConst(toRemove))
            delete takeConnection(con);
    }

    for (Connection *newConn : qAsConst(newList)) {
        bool found = false;
        const int c = connectionCount();
        for (int i = 0; i < c; i++) {
            Connection *con = connection(i);
            if (con->object(EndPoint::Source) == newConn->object(EndPoint::Source) &&
                            con->object(EndPoint::Target) == newConn->object(EndPoint::Target)) {
                found = true;
                break;
            }
        }
        if (found) {
            delete newConn;
        } else {
            AddConnectionCommand command(this, newConn);
            command.redo();
        }
    }
    m_updating = false;
}

void BuddyEditor::setBackground(QWidget *background)
{
    clear();
    ConnectionEdit::setBackground(background);
    if (background == nullptr)
        return;

    const auto label_list = background->findChildren<QLabel*>();
    for (QLabel *label : label_list) {
        const QString buddy_name = buddy(label, m_formWindow->core());
        if (buddy_name.isEmpty())
            continue;
        QWidget *target = background->findChild<QWidget*>(buddy_name);
        if (target == nullptr)
            continue;

        Connection *con = new Connection(this);
        con->setEndPoint(EndPoint::Source, label, widgetRect(label).center());
        con->setEndPoint(EndPoint::Target, target, widgetRect(target).center());
        addConnection(con);
    }
}

static QUndoCommand *createBuddyCommand(QDesignerFormWindowInterface *fw, QLabel *label, QWidget *buddy)
{
    SetPropertyCommand *command = new SetPropertyCommand(fw);
    command->init(label, QLatin1String(buddyPropertyC), buddy->objectName());
    command->setText(BuddyEditor::tr("Add buddy"));
    return command;
}

void BuddyEditor::endConnection(QWidget *target, const QPoint &pos)
{
    Connection *tmp_con = newlyAddedConnection();
    Q_ASSERT(tmp_con != nullptr);

    tmp_con->setEndPoint(EndPoint::Target, target, pos);

    QWidget *source = tmp_con->widget(EndPoint::Source);
    Q_ASSERT(source != nullptr);
    Q_ASSERT(target != nullptr);
    setEnabled(false);
    Connection *new_con = createConnection(source, target);
    setEnabled(true);
    if (new_con != nullptr) {
        new_con->setEndPoint(EndPoint::Source, source, tmp_con->endPointPos(EndPoint::Source));
        new_con->setEndPoint(EndPoint::Target, target, tmp_con->endPointPos(EndPoint::Target));

        selectNone();
        addConnection(new_con);
        QLabel *source = qobject_cast<QLabel*>(new_con->widget(EndPoint::Source));
        QWidget *target = new_con->widget(EndPoint::Target);
        if (source) {
            undoStack()->push(createBuddyCommand(m_formWindow, source, target));
        } else {
            qDebug("BuddyEditor::endConnection(): not a label");
        }
        setSelected(new_con, true);
    }

    clearNewlyAddedConnection();
    findObjectsUnderMouse(mapFromGlobal(QCursor::pos()));
}

void BuddyEditor::widgetRemoved(QWidget *widget)
{
    QWidgetList child_list = widget->findChildren<QWidget*>();
    child_list.prepend(widget);

    ConnectionSet remove_set;
    for (QWidget *w : qAsConst(child_list)) {
        const ConnectionList &cl = connectionList();
        for (Connection *con : cl) {
            if (con->widget(EndPoint::Source) == w || con->widget(EndPoint::Target) == w)
                remove_set.insert(con, con);
        }
    }

    if (!remove_set.isEmpty()) {
        undoStack()->beginMacro(tr("Remove buddies"));
        for (Connection *con : qAsConst(remove_set)) {
            setSelected(con, false);
            con->update();
            QWidget *source = con->widget(EndPoint::Source);
            if (qobject_cast<QLabel*>(source) == 0) {
                qDebug("BuddyConnection::widgetRemoved(): not a label");
            } else {
                ResetPropertyCommand *command = new ResetPropertyCommand(formWindow());
                command->init(source, QLatin1String(buddyPropertyC));
                undoStack()->push(command);
            }
            delete takeConnection(con);
        }
        undoStack()->endMacro();
    }
}

void BuddyEditor::deleteSelected()
{
    const ConnectionSet selectedConnections = selection(); // want copy for unselect
    if (selectedConnections.isEmpty())
        return;

    undoStack()->beginMacro(tr("Remove %n buddies", nullptr, selectedConnections.size()));
    for (Connection *con : selectedConnections) {
        setSelected(con, false);
        con->update();
        QWidget *source = con->widget(EndPoint::Source);
        if (qobject_cast<QLabel*>(source) == 0) {
            qDebug("BuddyConnection::deleteSelected(): not a label");
        } else {
            ResetPropertyCommand *command = new ResetPropertyCommand(formWindow());
            command->init(source, QLatin1String(buddyPropertyC));
            undoStack()->push(command);
        }
        delete takeConnection(con);
    }
    undoStack()->endMacro();
}

void BuddyEditor::autoBuddy()
{
    // Any labels?
    auto labelList = background()->findChildren<QLabel*>();
    if (labelList.isEmpty())
        return;
    // Find already used buddies
    QWidgetList usedBuddies;
    const ConnectionList &beforeConnections = connectionList();
    for (const Connection *c : beforeConnections)
        usedBuddies.push_back(c->widget(EndPoint::Target));
    // Find potential new buddies, keep lists in sync
    QWidgetList buddies;
    for (auto it = labelList.begin(); it != labelList.end(); ) {
        QLabel *label = *it;
        QWidget *newBuddy = nullptr;
        if (m_formWindow->isManaged(label)) {
            const QString buddy_name = buddy(label, m_formWindow->core());
            if (buddy_name.isEmpty())
                newBuddy = findBuddy(label, usedBuddies);
        }
        if (newBuddy) {
            buddies.push_back(newBuddy);
            usedBuddies.push_back(newBuddy);
            ++it;
        } else {
            it = labelList.erase(it);
        }
    }
    // Add the list in one go.
    if (labelList.isEmpty())
        return;
    const int count = labelList.size();
    Q_ASSERT(count == buddies.size());
    undoStack()->beginMacro(tr("Add %n buddies", nullptr, count));
    for (int i = 0; i < count; i++)
        undoStack()->push(createBuddyCommand(m_formWindow, labelList.at(i), buddies.at(i)));
    undoStack()->endMacro();
    // Now select all new ones
    const ConnectionList &connections = connectionList();
    for (Connection *con : connections)
        setSelected(con, buddies.contains(con->widget(EndPoint::Target)));
}

// Geometrically find  a potential buddy for label by checking neighbouring children of parent
QWidget *BuddyEditor::findBuddy(QLabel *l, const QWidgetList &existingBuddies) const
{
    enum { DeltaX = 5 };
    const QWidget *parent = l->parentWidget();
    // Try to find next managed neighbour on horizontal line
    const QRect geom = l->geometry();
    const int y = geom.center().y();
    QWidget *neighbour = nullptr;
    switch (l->layoutDirection()) {
    case Qt::LayoutDirectionAuto:
    case Qt::LeftToRight: { // Walk right to find next managed neighbour
        const int xEnd = parent->size().width();
        for (int x = geom.right() + 1; x < xEnd; x += DeltaX)
            if (QWidget *c = parent->childAt (x, y))
                if (m_formWindow->isManaged(c)) {
                    neighbour = c;
                    break;
                }
    }
        break;
    case Qt::RightToLeft:  // Walk left to find next managed neighbour
        for (int x = geom.x() - 1; x >= 0; x -= DeltaX)
            if (QWidget *c = parent->childAt (x, y))
                if (m_formWindow->isManaged(c)) {
                    neighbour = c;
                    break;
                }
        break;
    }
    if (neighbour && !existingBuddies.contains(neighbour) && canBeBuddy(neighbour, m_formWindow))
        return neighbour;

    return nullptr;
}

void BuddyEditor::createContextMenu(QMenu &menu)
{
    QAction *autoAction = menu.addAction(tr("Set automatically"));
    connect(autoAction, &QAction::triggered, this, &BuddyEditor::autoBuddy);
    menu.addSeparator();
    ConnectionEdit::createContextMenu(menu);
}

}

QT_END_NAMESPACE
