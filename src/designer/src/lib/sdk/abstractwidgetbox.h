// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef ABSTRACTWIDGETBOX_H
#define ABSTRACTWIDGETBOX_H

#include <QtDesigner/sdk_global.h>

#include <QtCore/qshareddata.h>
#include <QtCore/qmetatype.h>
#include <QtWidgets/qwidget.h>
#include <QtGui/qicon.h>

QT_BEGIN_NAMESPACE

class DomUI;
class QDesignerDnDItemInterface;

class QDesignerWidgetBoxWidgetData;

class QDESIGNER_SDK_EXPORT QDesignerWidgetBoxInterface : public QWidget
{
    Q_OBJECT
public:
    class QDESIGNER_SDK_EXPORT Widget
    {
    public:
        enum Type { Default, Custom };
        Widget(const QString &aname = QString(), const QString &xml = QString(),
                const QString &icon_name = QString(), Type atype = Default);
        ~Widget();
        Widget(const Widget &w);
        Widget &operator=(const Widget &w);

        QString name() const;
        void setName(const QString &aname);
        QString domXml() const;
        void setDomXml(const QString &xml);
        QString iconName() const;
        void setIconName(const QString &icon_name);
        Type type() const;
        void setType(Type atype);

        bool isNull() const;

    private:
        QSharedDataPointer<QDesignerWidgetBoxWidgetData> m_data;
    };

    using WidgetList = QList<Widget>;

    class Category
    {
    public:
        enum Type { Default, Scratchpad };

        Category(const QString &aname = QString(), Type atype = Default)
            : m_name(aname), m_type(atype) {}

        QString name() const { return m_name; }
        void setName(const QString &aname) { m_name = aname; }
        int widgetCount() const { return m_widget_list.size(); }
        Widget widget(int idx) const { return m_widget_list.at(idx); }
        void removeWidget(int idx) { m_widget_list.removeAt(idx); }
        void addWidget(const Widget &awidget) { m_widget_list.append(awidget); }
        Type type() const { return m_type; }
        void setType(Type atype) { m_type = atype; }

        bool isNull() const { return m_name.isEmpty(); }

    private:
        QString m_name;
        Type m_type;
        WidgetList m_widget_list;
    };

    using CategoryList = QList<Category>;

    explicit QDesignerWidgetBoxInterface(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
    virtual ~QDesignerWidgetBoxInterface();

    virtual int categoryCount() const = 0;
    virtual Category category(int cat_idx) const = 0;
    virtual void addCategory(const Category &cat) = 0;
    virtual void removeCategory(int cat_idx) = 0;

    virtual int widgetCount(int cat_idx) const = 0;
    virtual Widget widget(int cat_idx, int wgt_idx) const = 0;
    virtual void addWidget(int cat_idx, const Widget &wgt) = 0;
    virtual void removeWidget(int cat_idx, int wgt_idx) = 0;

    int findOrInsertCategory(const QString &categoryName);

    virtual void dropWidgets(const QList<QDesignerDnDItemInterface*> &item_list,
                                const QPoint &global_mouse_pos) = 0;

    virtual void setFileName(const QString &file_name) = 0;
    virtual QString fileName() const = 0;
    virtual bool load() = 0;
    virtual bool save() = 0;
};

QT_END_NAMESPACE

Q_DECLARE_METATYPE(QT_PREPEND_NAMESPACE(QDesignerWidgetBoxInterface::Widget))

#endif // ABSTRACTWIDGETBOX_H
