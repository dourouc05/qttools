// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlpropertynode.h"

#include "classnode.h"
#include "propertynode.h"

#include <utility>
#include "qdocdatabase.h"

QT_BEGIN_NAMESPACE

/*!
  Constructor for the QML property node.
 */
QmlPropertyNode::QmlPropertyNode(Aggregate *parent, const QString &name, QString type,
                                 bool attached)
    : Node(parent->isJsType() ? JsProperty : QmlProperty, parent, name),
      m_type(std::move(type)),
      m_attached(attached)
{
    if (m_type == "alias")
        m_isAlias = true;
    if (name.startsWith("__"))
        setStatus(Internal);
}

/*!
  Returns \c true if a QML property or attached property is
  not read-only. The algorithm for figuring this out is long
  amd tedious and almost certainly will break. It currently
  doesn't work for the qmlproperty:

  \code
      bool PropertyChanges::explicit,
  \endcode

  ...because the tokenizer gets confused on \e{explicit}.
 */
bool QmlPropertyNode::isWritable()
{
    if (readOnly_ != FlagValueDefault)
        return !fromFlagValue(readOnly_, false);

    QmlTypeNode *qcn = qmlTypeNode();
    if (qcn) {
        if (qcn->cppClassRequired()) {
            if (qcn->classNode()) {
                PropertyNode *pn = findCorrespondingCppProperty();
                if (pn)
                    return pn->isWritable();
                else
                    defLocation().warning(
                            QStringLiteral(
                                    "No Q_PROPERTY for QML property %1::%2::%3 "
                                    "in C++ class documented as QML type: "
                                    "(property not found in the C++ class or its base classes)")
                                    .arg(logicalModuleName(), qmlTypeName(), name()));
            } else
                defLocation().warning(QStringLiteral("No Q_PROPERTY for QML property %1::%2::%3 "
                                                     "in C++ class documented as QML type: "
                                                     "(C++ class not specified or not found).")
                                              .arg(logicalModuleName(), qmlTypeName(), name()));
        }
    }
    return true;
}

/*!
    Returns \c true if this QML property is marked with \required or the
    corresponding C++ property uses the REQUIRED keyword.
*/
bool QmlPropertyNode::isRequired()
{
    if (m_required != FlagValueDefault)
        return fromFlagValue(m_required, false);

    PropertyNode *pn = findCorrespondingCppProperty();
    return pn != nullptr && pn->isRequired();
}

/*!
  Returns a pointer this QML property's corresponding C++
  property, if it has one.
 */
PropertyNode *QmlPropertyNode::findCorrespondingCppProperty()
{
    PropertyNode *pn;
    Node *n = parent();
    while (n && !(n->isQmlType() || n->isJsType()))
        n = n->parent();
    if (n) {
        auto *qcn = static_cast<QmlTypeNode *>(n);
        ClassNode *cn = qcn->classNode();
        if (cn) {
            /*
              If there is a dot in the property name, first
              find the C++ property corresponding to the QML
              property group.
             */
            QStringList dotSplit = name().split(QChar('.'));
            pn = cn->findPropertyNode(dotSplit[0]);
            if (pn) {
                /*
                  Now find the C++ property corresponding to
                  the QML property in the QML property group,
                  <group>.<property>.
                 */
                if (dotSplit.size() > 1) {
                    QStringList path(extractClassName(pn->qualifiedDataType()));
                    Node *nn = QDocDatabase::qdocDB()->findClassNode(path);
                    if (nn) {
                        auto *cn = static_cast<ClassNode *>(nn);
                        PropertyNode *pn2 = cn->findPropertyNode(dotSplit[1]);
                        /*
                          If found, return the C++ property
                          corresponding to the QML property.
                          Otherwise, return the C++ property
                          corresponding to the QML property
                          group.
                         */
                        return (pn2 ? pn2 : pn);
                    }
                } else
                    return pn;
            }
        }
    }
    return nullptr;
}

QT_END_NAMESPACE
