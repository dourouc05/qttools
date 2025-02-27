// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlmarkupvisitor.h"

#include <QtCore/qglobal.h>
#include <QtCore/qstringlist.h>

#ifndef QT_NO_DECLARATIVE
#    include <private/qqmljsast_p.h>
#    include <private/qqmljsengine_p.h>
#endif

QT_BEGIN_NAMESPACE

#ifndef QT_NO_DECLARATIVE
QmlMarkupVisitor::QmlMarkupVisitor(const QString &source,
                                   const QList<QQmlJS::SourceLocation> &pragmas,
                                   QQmlJS::Engine *engine)
{
    this->m_source = source;
    this->m_engine = engine;

    m_cursor = 0;
    m_extraIndex = 0;

    // Merge the lists of locations of pragmas and comments in the source code.
    int i = 0;
    int j = 0;
    const QList<QQmlJS::SourceLocation> comments = engine->comments();
    while (i < comments.size() && j < pragmas.length()) {
        if (comments[i].offset < pragmas[j].offset) {
            m_extraTypes.append(Comment);
            m_extraLocations.append(comments[i]);
            ++i;
        } else {
            m_extraTypes.append(Pragma);
            m_extraLocations.append(comments[j]);
            ++j;
        }
    }

    while (i < comments.size()) {
        m_extraTypes.append(Comment);
        m_extraLocations.append(comments[i]);
        ++i;
    }

    while (j < pragmas.length()) {
        m_extraTypes.append(Pragma);
        m_extraLocations.append(pragmas[j]);
        ++j;
    }
}

// The protect() function is a copy of the one from CppCodeMarker.

static const QString samp = QLatin1String("&amp;");
static const QString slt = QLatin1String("&lt;");
static const QString sgt = QLatin1String("&gt;");
static const QString squot = QLatin1String("&quot;");

QString QmlMarkupVisitor::protect(const QString &str)
{
    qsizetype n = str.length();
    QString marked;
    marked.reserve(n * 2 + 30);
    const QChar *data = str.constData();
    for (int i = 0; i != n; ++i) {
        switch (data[i].unicode()) {
        case '&':
            marked += samp;
            break;
        case '<':
            marked += slt;
            break;
        case '>':
            marked += sgt;
            break;
        case '"':
            marked += squot;
            break;
        default:
            marked += data[i];
        }
    }
    return marked;
}

QString QmlMarkupVisitor::markedUpCode()
{
    if (int(m_cursor) < m_source.length())
        addExtra(m_cursor, m_source.length());

    return m_output;
}

bool QmlMarkupVisitor::hasError() const
{
    return m_hasRecursionDepthError;
}

void QmlMarkupVisitor::addExtra(quint32 start, quint32 finish)
{
    if (m_extraIndex >= m_extraLocations.length()) {
        QString extra = m_source.mid(start, finish - start);
        if (extra.trimmed().isEmpty())
            m_output += extra;
        else
            m_output += protect(extra); // text that should probably have been caught by the parser

        m_cursor = finish;
        return;
    }

    while (m_extraIndex < m_extraLocations.length()) {
        if (m_extraTypes[m_extraIndex] == Comment) {
            if (m_extraLocations[m_extraIndex].offset - 2 >= start)
                break;
        } else {
            if (m_extraLocations[m_extraIndex].offset >= start)
                break;
        }
        m_extraIndex++;
    }

    quint32 i = start;
    while (i < finish && m_extraIndex < m_extraLocations.length()) {
        quint32 j = m_extraLocations[m_extraIndex].offset - 2;
        if (i <= j && j < finish) {
            if (i < j)
                m_output += protect(m_source.mid(i, j - i));

            quint32 l = m_extraLocations[m_extraIndex].length;
            if (m_extraTypes[m_extraIndex] == Comment) {
                if (m_source.mid(j, 2) == QLatin1String("/*"))
                    l += 4;
                else
                    l += 2;
                m_output += QLatin1String("<@comment>");
                m_output += protect(m_source.mid(j, l));
                m_output += QLatin1String("</@comment>");
            } else
                m_output += protect(m_source.mid(j, l));

            m_extraIndex++;
            i = j + l;
        } else
            break;
    }

    QString extra = m_source.mid(i, finish - i);
    if (extra.trimmed().isEmpty())
        m_output += extra;
    else
        m_output += protect(extra); // text that should probably have been caught by the parser

    m_cursor = finish;
}

void QmlMarkupVisitor::addMarkedUpToken(QQmlJS::SourceLocation &location,
                                        const QString &tagName,
                                        const QHash<QString, QString> &attributes)
{
    if (!location.isValid())
        return;

    if (m_cursor < location.offset)
        addExtra(m_cursor, location.offset);
    else if (m_cursor > location.offset)
        return;

    m_output += QString(QLatin1String("<@%1")).arg(tagName);
    for (const auto &key : attributes)
        m_output += QString(QLatin1String(" %1=\"%2\"")).arg(key, attributes[key]);
    m_output += QString(QLatin1String(">%2</@%3>")).arg(protect(sourceText(location)), tagName);
    m_cursor += location.length;
}

QString QmlMarkupVisitor::sourceText(QQmlJS::SourceLocation &location)
{
    return m_source.mid(location.offset, location.length);
}

void QmlMarkupVisitor::addVerbatim(QQmlJS::SourceLocation first,
                                   QQmlJS::SourceLocation last)
{
    if (!first.isValid())
        return;

    quint32 start = first.begin();
    quint32 finish;
    if (last.isValid())
        finish = last.end();
    else
        finish = first.end();

    if (m_cursor < start)
        addExtra(m_cursor, start);
    else if (m_cursor > start)
        return;

    QString text = m_source.mid(start, finish - start);
    m_output += protect(text);
    m_cursor = finish;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiImport *uiimport)
{
    addVerbatim(uiimport->importToken);
    if (!uiimport->importUri)
        addMarkedUpToken(uiimport->fileNameToken, QLatin1String("headerfile"));
    return false;
}

void QmlMarkupVisitor::endVisit(QQmlJS::AST::UiImport *uiimport)
{
    if (uiimport->version)
        addVerbatim(uiimport->version->firstSourceLocation(),
                    uiimport->version->lastSourceLocation());
    addVerbatim(uiimport->asToken);
    addMarkedUpToken(uiimport->importIdToken, QLatin1String("headerfile"));
    addVerbatim(uiimport->semicolonToken);
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiPublicMember *member)
{
    if (member->type == QQmlJS::AST::UiPublicMember::Property) {
        addVerbatim(member->defaultToken());
        addVerbatim(member->readonlyToken());
        addVerbatim(member->propertyToken());
        addVerbatim(member->typeModifierToken);
        addMarkedUpToken(member->typeToken, QLatin1String("type"));
        addMarkedUpToken(member->identifierToken, QLatin1String("name"));
        addVerbatim(member->colonToken);
        if (member->binding)
            QQmlJS::AST::Node::accept(member->binding, this);
        else if (member->statement)
            QQmlJS::AST::Node::accept(member->statement, this);
    } else {
        addVerbatim(member->propertyToken());
        addVerbatim(member->typeModifierToken);
        addMarkedUpToken(member->typeToken, QLatin1String("type"));
        // addVerbatim(member->identifierToken());
        QQmlJS::AST::Node::accept(member->parameters, this);
    }
    addVerbatim(member->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiObjectInitializer *initializer)
{
    addVerbatim(initializer->lbraceToken, initializer->lbraceToken);
    return true;
}

void QmlMarkupVisitor::endVisit(QQmlJS::AST::UiObjectInitializer *initializer)
{
    addVerbatim(initializer->rbraceToken, initializer->rbraceToken);
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiObjectBinding *binding)
{
    QQmlJS::AST::Node::accept(binding->qualifiedId, this);
    addVerbatim(binding->colonToken);
    QQmlJS::AST::Node::accept(binding->qualifiedTypeNameId, this);
    QQmlJS::AST::Node::accept(binding->initializer, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiScriptBinding *binding)
{
    QQmlJS::AST::Node::accept(binding->qualifiedId, this);
    addVerbatim(binding->colonToken);
    QQmlJS::AST::Node::accept(binding->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiArrayBinding *binding)
{
    QQmlJS::AST::Node::accept(binding->qualifiedId, this);
    addVerbatim(binding->colonToken);
    addVerbatim(binding->lbracketToken);
    QQmlJS::AST::Node::accept(binding->members, this);
    addVerbatim(binding->rbracketToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiArrayMemberList *list)
{
    for (QQmlJS::AST::UiArrayMemberList *it = list; it; it = it->next) {
        QQmlJS::AST::Node::accept(it->member, this);
        // addVerbatim(it->commaToken);
    }
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiQualifiedId *id)
{
    addMarkedUpToken(id->identifierToken, QLatin1String("name"));
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ThisExpression *expression)
{
    addVerbatim(expression->thisToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::IdentifierExpression *identifier)
{
    addMarkedUpToken(identifier->identifierToken, QLatin1String("name"));
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::NullExpression *null)
{
    addMarkedUpToken(null->nullToken, QLatin1String("number"));
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::TrueLiteral *literal)
{
    addMarkedUpToken(literal->trueToken, QLatin1String("number"));
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::FalseLiteral *literal)
{
    addMarkedUpToken(literal->falseToken, QLatin1String("number"));
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::NumericLiteral *literal)
{
    addMarkedUpToken(literal->literalToken, QLatin1String("number"));
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::StringLiteral *literal)
{
    addMarkedUpToken(literal->literalToken, QLatin1String("string"));
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::RegExpLiteral *literal)
{
    addVerbatim(literal->literalToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ArrayPattern *literal)
{
    addVerbatim(literal->lbracketToken);
    QQmlJS::AST::Node::accept(literal->elements, this);
    addVerbatim(literal->rbracketToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ObjectPattern *literal)
{
    addVerbatim(literal->lbraceToken);
    return true;
}

void QmlMarkupVisitor::endVisit(QQmlJS::AST::ObjectPattern *literal)
{
    addVerbatim(literal->rbraceToken);
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PatternElementList *list)
{
    for (QQmlJS::AST::PatternElementList *it = list; it; it = it->next) {
        QQmlJS::AST::Node::accept(it->element, this);
        // addVerbatim(it->commaToken);
    }
    QQmlJS::AST::Node::accept(list->elision, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::Elision *elision)
{
    addVerbatim(elision->commaToken, elision->commaToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PatternProperty *list)
{
    QQmlJS::AST::Node::accept(list->name, this);
    addVerbatim(list->colonToken, list->colonToken);
    QQmlJS::AST::Node::accept(list->initializer, this);
    // addVerbatim(list->commaToken, list->commaToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ArrayMemberExpression *expression)
{
    QQmlJS::AST::Node::accept(expression->base, this);
    addVerbatim(expression->lbracketToken);
    QQmlJS::AST::Node::accept(expression->expression, this);
    addVerbatim(expression->rbracketToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::FieldMemberExpression *expression)
{
    QQmlJS::AST::Node::accept(expression->base, this);
    addVerbatim(expression->dotToken);
    addMarkedUpToken(expression->identifierToken, QLatin1String("name"));
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::NewMemberExpression *expression)
{
    addVerbatim(expression->newToken);
    QQmlJS::AST::Node::accept(expression->base, this);
    addVerbatim(expression->lparenToken);
    QQmlJS::AST::Node::accept(expression->arguments, this);
    addVerbatim(expression->rparenToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::NewExpression *expression)
{
    addVerbatim(expression->newToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ArgumentList *list)
{
    addVerbatim(list->commaToken, list->commaToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PostIncrementExpression *expression)
{
    addVerbatim(expression->incrementToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PostDecrementExpression *expression)
{
    addVerbatim(expression->decrementToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::DeleteExpression *expression)
{
    addVerbatim(expression->deleteToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::VoidExpression *expression)
{
    addVerbatim(expression->voidToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::TypeOfExpression *expression)
{
    addVerbatim(expression->typeofToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PreIncrementExpression *expression)
{
    addVerbatim(expression->incrementToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::PreDecrementExpression *expression)
{
    addVerbatim(expression->decrementToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UnaryPlusExpression *expression)
{
    addVerbatim(expression->plusToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::UnaryMinusExpression *expression)
{
    addVerbatim(expression->minusToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::TildeExpression *expression)
{
    addVerbatim(expression->tildeToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::NotExpression *expression)
{
    addVerbatim(expression->notToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::BinaryExpression *expression)
{
    QQmlJS::AST::Node::accept(expression->left, this);
    addMarkedUpToken(expression->operatorToken, QLatin1String("op"));
    QQmlJS::AST::Node::accept(expression->right, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ConditionalExpression *expression)
{
    QQmlJS::AST::Node::accept(expression->expression, this);
    addVerbatim(expression->questionToken);
    QQmlJS::AST::Node::accept(expression->ok, this);
    addVerbatim(expression->colonToken);
    QQmlJS::AST::Node::accept(expression->ko, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::Expression *expression)
{
    QQmlJS::AST::Node::accept(expression->left, this);
    addVerbatim(expression->commaToken);
    QQmlJS::AST::Node::accept(expression->right, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::Block *block)
{
    addVerbatim(block->lbraceToken);
    return true;
}

void QmlMarkupVisitor::endVisit(QQmlJS::AST::Block *block)
{
    addVerbatim(block->rbraceToken);
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::VariableStatement *statement)
{
    addVerbatim(statement->declarationKindToken);
    QQmlJS::AST::Node::accept(statement->declarations, this);
    // addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::VariableDeclarationList *list)
{
    for (QQmlJS::AST::VariableDeclarationList *it = list; it; it = it->next) {
        QQmlJS::AST::Node::accept(it->declaration, this);
        addVerbatim(it->commaToken);
    }
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::EmptyStatement *statement)
{
    addVerbatim(statement->semicolonToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ExpressionStatement *statement)
{
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::IfStatement *statement)
{
    addMarkedUpToken(statement->ifToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->ok, this);
    if (statement->ko) {
        addMarkedUpToken(statement->elseToken, QLatin1String("keyword"));
        QQmlJS::AST::Node::accept(statement->ko, this);
    }
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::DoWhileStatement *statement)
{
    addMarkedUpToken(statement->doToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(statement->statement, this);
    addMarkedUpToken(statement->whileToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::WhileStatement *statement)
{
    addMarkedUpToken(statement->whileToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ForStatement *statement)
{
    addMarkedUpToken(statement->forToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->initialiser, this);
    addVerbatim(statement->firstSemicolonToken);
    QQmlJS::AST::Node::accept(statement->condition, this);
    addVerbatim(statement->secondSemicolonToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ForEachStatement *statement)
{
    addMarkedUpToken(statement->forToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->lhs, this);
    addVerbatim(statement->inOfToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ContinueStatement *statement)
{
    addMarkedUpToken(statement->continueToken, QLatin1String("keyword"));
    addMarkedUpToken(statement->identifierToken, QLatin1String("name"));
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::BreakStatement *statement)
{
    addMarkedUpToken(statement->breakToken, QLatin1String("keyword"));
    addMarkedUpToken(statement->identifierToken, QLatin1String("name"));
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ReturnStatement *statement)
{
    addMarkedUpToken(statement->returnToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::WithStatement *statement)
{
    addMarkedUpToken(statement->withToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::CaseBlock *block)
{
    addVerbatim(block->lbraceToken);
    return true;
}

void QmlMarkupVisitor::endVisit(QQmlJS::AST::CaseBlock *block)
{
    addVerbatim(block->rbraceToken, block->rbraceToken);
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::SwitchStatement *statement)
{
    addMarkedUpToken(statement->switchToken, QLatin1String("keyword"));
    addVerbatim(statement->lparenToken);
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->rparenToken);
    QQmlJS::AST::Node::accept(statement->block, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::CaseClause *clause)
{
    addMarkedUpToken(clause->caseToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(clause->expression, this);
    addVerbatim(clause->colonToken);
    QQmlJS::AST::Node::accept(clause->statements, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::DefaultClause *clause)
{
    addMarkedUpToken(clause->defaultToken, QLatin1String("keyword"));
    addVerbatim(clause->colonToken, clause->colonToken);
    return true;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::LabelledStatement *statement)
{
    addMarkedUpToken(statement->identifierToken, QLatin1String("name"));
    addVerbatim(statement->colonToken);
    QQmlJS::AST::Node::accept(statement->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::ThrowStatement *statement)
{
    addMarkedUpToken(statement->throwToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(statement->expression, this);
    addVerbatim(statement->semicolonToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::Catch *c)
{
    addMarkedUpToken(c->catchToken, QLatin1String("keyword"));
    addVerbatim(c->lparenToken);
    addMarkedUpToken(c->identifierToken, QLatin1String("name"));
    addVerbatim(c->rparenToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::Finally *f)
{
    addMarkedUpToken(f->finallyToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(f->statement, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::TryStatement *statement)
{
    addMarkedUpToken(statement->tryToken, QLatin1String("keyword"));
    QQmlJS::AST::Node::accept(statement->statement, this);
    QQmlJS::AST::Node::accept(statement->catchExpression, this);
    QQmlJS::AST::Node::accept(statement->finallyExpression, this);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::FunctionExpression *expression)
{
    addMarkedUpToken(expression->functionToken, QLatin1String("keyword"));
    addMarkedUpToken(expression->identifierToken, QLatin1String("name"));
    addVerbatim(expression->lparenToken);
    QQmlJS::AST::Node::accept(expression->formals, this);
    addVerbatim(expression->rparenToken);
    addVerbatim(expression->lbraceToken);
    QQmlJS::AST::Node::accept(expression->body, this);
    addVerbatim(expression->rbraceToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::FunctionDeclaration *declaration)
{
    addMarkedUpToken(declaration->functionToken, QLatin1String("keyword"));
    addMarkedUpToken(declaration->identifierToken, QLatin1String("name"));
    addVerbatim(declaration->lparenToken);
    QQmlJS::AST::Node::accept(declaration->formals, this);
    addVerbatim(declaration->rparenToken);
    addVerbatim(declaration->lbraceToken);
    QQmlJS::AST::Node::accept(declaration->body, this);
    addVerbatim(declaration->rbraceToken);
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::FormalParameterList *list)
{
    //    addVerbatim(list->commaToken);
    QQmlJS::AST::Node::accept(list->element, this);
    // addMarkedUpToken(list->identifierToken, QLatin1String("name"));
    return false;
}

bool QmlMarkupVisitor::visit(QQmlJS::AST::DebuggerStatement *statement)
{
    addVerbatim(statement->debuggerToken);
    addVerbatim(statement->semicolonToken);
    return true;
}

// Elements and items are represented by UiObjectDefinition nodes.

bool QmlMarkupVisitor::visit(QQmlJS::AST::UiObjectDefinition *definition)
{
    addMarkedUpToken(definition->qualifiedTypeNameId->identifierToken, QLatin1String("type"));
    QQmlJS::AST::Node::accept(definition->initializer, this);
    return false;
}

void QmlMarkupVisitor::throwRecursionDepthError()
{
    m_hasRecursionDepthError = true;
}

#endif

QT_END_NAMESPACE
