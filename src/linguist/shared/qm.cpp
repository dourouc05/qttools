// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "translator.h"

#ifndef QT_BOOTSTRAPPED
#include <QtCore/QCoreApplication>
#endif
#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringDecoder>

QT_BEGIN_NAMESPACE

// magic number for the file
static const int MagicLength = 16;
static const uchar magic[MagicLength] = {
    0x3c, 0xb8, 0x64, 0x18, 0xca, 0xef, 0x9c, 0x95,
    0xcd, 0x21, 0x1c, 0xbf, 0x60, 0xa1, 0xbd, 0xdd
};


namespace {

enum Tag {
    Tag_End          = 1,
    Tag_SourceText16 = 2,
    Tag_Translation  = 3,
    Tag_Context16    = 4,
    Tag_Obsolete1    = 5,
    Tag_SourceText   = 6,
    Tag_Context      = 7,
    Tag_Comment      = 8,
    Tag_Obsolete2    = 9
};

enum Prefix {
    NoPrefix,
    Hash,
    HashContext,
    HashContextSourceText,
    HashContextSourceTextComment
};

} // namespace anon

static uint elfHash(const QByteArray &ba)
{
    const uchar *k = (const uchar *)ba.data();
    uint h = 0;
    uint g;

    if (k) {
        while (*k) {
            h = (h << 4) + *k++;
            if ((g = (h & 0xf0000000)) != 0)
                h ^= g >> 24;
            h &= ~g;
        }
    }
    if (!h)
        h = 1;
    return h;
}

class ByteTranslatorMessage
{
public:
    ByteTranslatorMessage(
            const QByteArray &context,
            const QByteArray &sourceText,
            const QByteArray &comment,
            const QStringList &translations) :
        m_context(context),
        m_sourcetext(sourceText),
        m_comment(comment),
        m_translations(translations)
    {}
    const QByteArray &context() const { return m_context; }
    const QByteArray &sourceText() const { return m_sourcetext; }
    const QByteArray &comment() const { return m_comment; }
    const QStringList &translations() const { return m_translations; }
    bool operator<(const ByteTranslatorMessage& m) const;

private:
    QByteArray m_context;
    QByteArray m_sourcetext;
    QByteArray m_comment;
    QStringList m_translations;
};

Q_DECLARE_TYPEINFO(ByteTranslatorMessage, Q_RELOCATABLE_TYPE);

bool ByteTranslatorMessage::operator<(const ByteTranslatorMessage& m) const
{
    if (m_context != m.m_context)
        return m_context < m.m_context;
    if (m_sourcetext != m.m_sourcetext)
        return m_sourcetext < m.m_sourcetext;
    return m_comment < m.m_comment;
}

class Releaser
{
public:
    struct Offset {
        Offset()
            : h(0), o(0)
        {}
        Offset(uint hash, uint offset)
            : h(hash), o(offset)
        {}

        bool operator<(const Offset &other) const {
            return (h != other.h) ? h < other.h : o < other.o;
        }
        bool operator==(const Offset &other) const {
            return h == other.h && o == other.o;
        }
        uint h;
        uint o;
    };

    enum { Contexts = 0x2f, Hashes = 0x42, Messages = 0x69, NumerusRules = 0x88, Dependencies = 0x96, Language = 0xa7 };

    Releaser(const QString &language) : m_language(language) {}

    bool save(QIODevice *iod);

    void insert(const TranslatorMessage &msg, const QStringList &tlns, bool forceComment);
    void insertIdBased(const TranslatorMessage &message, const QStringList &tlns);

    void squeeze(TranslatorSaveMode mode);

    void setNumerusRules(const QByteArray &rules);
    void setDependencies(const QStringList &dependencies);

private:
    Q_DISABLE_COPY(Releaser)

    // This should reproduce the byte array fetched from the source file, which
    // on turn should be the same as passed to the actual tr(...) calls
    QByteArray originalBytes(const QString &str) const;

    static Prefix commonPrefix(const ByteTranslatorMessage &m1, const ByteTranslatorMessage &m2);

    static uint msgHash(const ByteTranslatorMessage &msg);

    void writeMessage(const ByteTranslatorMessage & msg, QDataStream & stream,
        TranslatorSaveMode strip, Prefix prefix) const;

    QString m_language;
    // for squeezed but non-file data, this is what needs to be deleted
    QByteArray m_messageArray;
    QByteArray m_offsetArray;
    QByteArray m_contextArray;
    QMap<ByteTranslatorMessage, void *> m_messages;
    QByteArray m_numerusRules;
    QStringList m_dependencies;
    QByteArray m_dependencyArray;
};

QByteArray Releaser::originalBytes(const QString &str) const
{
    if (str.isEmpty()) {
        // Do not use QByteArray() here as the result of the serialization
        // will be different.
        return QByteArray("");
    }
    return str.toUtf8();
}

uint Releaser::msgHash(const ByteTranslatorMessage &msg)
{
    return elfHash(msg.sourceText() + msg.comment());
}

Prefix Releaser::commonPrefix(const ByteTranslatorMessage &m1, const ByteTranslatorMessage &m2)
{
    if (msgHash(m1) != msgHash(m2))
        return NoPrefix;
    if (m1.context() != m2.context())
        return Hash;
    if (m1.sourceText() != m2.sourceText())
        return HashContext;
    if (m1.comment() != m2.comment())
        return HashContextSourceText;
    return HashContextSourceTextComment;
}

void Releaser::writeMessage(const ByteTranslatorMessage &msg, QDataStream &stream,
    TranslatorSaveMode mode, Prefix prefix) const
{
    for (int i = 0; i < msg.translations().count(); ++i)
        stream << quint8(Tag_Translation) << msg.translations().at(i);

    if (mode == SaveEverything)
        prefix = HashContextSourceTextComment;

    // lrelease produces "wrong" QM files for QByteArrays that are .isNull().
    switch (prefix) {
    default:
    case HashContextSourceTextComment:
        stream << quint8(Tag_Comment) << msg.comment();
        Q_FALLTHROUGH();
    case HashContextSourceText:
        stream << quint8(Tag_SourceText) << msg.sourceText();
        Q_FALLTHROUGH();
    case HashContext:
        stream << quint8(Tag_Context) << msg.context();
        break;
    }

    stream << quint8(Tag_End);
}


bool Releaser::save(QIODevice *iod)
{
    QDataStream s(iod);
    s.writeRawData((const char *)magic, MagicLength);

    if (!m_language.isEmpty()) {
        QByteArray lang = originalBytes(m_language);
        quint32 las = quint32(lang.size());
        s << quint8(Language) << las;
        s.writeRawData(lang, las);
    }
    if (!m_dependencyArray.isEmpty()) {
        quint32 das = quint32(m_dependencyArray.size());
        s << quint8(Dependencies) << das;
        s.writeRawData(m_dependencyArray.constData(), das);
    }
    if (!m_offsetArray.isEmpty()) {
        quint32 oas = quint32(m_offsetArray.size());
        s << quint8(Hashes) << oas;
        s.writeRawData(m_offsetArray.constData(), oas);
    }
    if (!m_messageArray.isEmpty()) {
        quint32 mas = quint32(m_messageArray.size());
        s << quint8(Messages) << mas;
        s.writeRawData(m_messageArray.constData(), mas);
    }
    if (!m_contextArray.isEmpty()) {
        quint32 cas = quint32(m_contextArray.size());
        s << quint8(Contexts) << cas;
        s.writeRawData(m_contextArray.constData(), cas);
    }
    if (!m_numerusRules.isEmpty()) {
        quint32 nrs = m_numerusRules.size();
        s << quint8(NumerusRules) << nrs;
        s.writeRawData(m_numerusRules.constData(), nrs);
    }
    return true;
}

void Releaser::squeeze(TranslatorSaveMode mode)
{
    m_dependencyArray.clear();
    QDataStream depstream(&m_dependencyArray, QIODevice::WriteOnly);
    for (const QString &dep : qAsConst(m_dependencies))
        depstream << dep;

    if (m_messages.isEmpty() && mode == SaveEverything)
        return;

    const auto messages = m_messages;

    // re-build contents
    m_messageArray.clear();
    m_offsetArray.clear();
    m_contextArray.clear();
    m_messages.clear();

    QMap<Offset, void *> offsets;

    QDataStream ms(&m_messageArray, QIODevice::WriteOnly);
    int cpPrev = 0, cpNext = 0;
    for (auto it = messages.cbegin(), end = messages.cend(); it != end; ++it) {
        cpPrev = cpNext;
        const auto next = std::next(it);
        if (next == end)
            cpNext = 0;
        else
            cpNext = commonPrefix(it.key(), next.key());
        offsets.insert(Offset(msgHash(it.key()), ms.device()->pos()), (void *)0);
        writeMessage(it.key(), ms, mode, Prefix(qMax(cpPrev, cpNext + 1)));
    }

    auto offset = offsets.cbegin();
    QDataStream ds(&m_offsetArray, QIODevice::WriteOnly);
    while (offset != offsets.cend()) {
        Offset k = offset.key();
        ++offset;
        ds << quint32(k.h) << quint32(k.o);
    }

    if (mode == SaveStripped) {
        QMap<QByteArray, int> contextSet;
        for (auto it = messages.cbegin(), end = messages.cend(); it != end; ++it)
            ++contextSet[it.key().context()];

        quint16 hTableSize;
        if (contextSet.size() < 200)
            hTableSize = (contextSet.size() < 60) ? 151 : 503;
        else if (contextSet.size() < 2500)
            hTableSize = (contextSet.size() < 750) ? 1511 : 5003;
        else
            hTableSize = (contextSet.size() < 10000) ? 15013 : 3 * contextSet.size() / 2;

        QMultiMap<int, QByteArray> hashMap;
        for (auto c = contextSet.cbegin(), end = contextSet.cend(); c != end; ++c)
            hashMap.insert(elfHash(c.key()) % hTableSize, c.key());

        /*
          The contexts found in this translator are stored in a hash
          table to provide fast lookup. The context array has the
          following format:

              quint16 hTableSize;
              quint16 hTable[hTableSize];
              quint8  contextPool[...];

          The context pool stores the contexts as Pascal strings:

              quint8  len;
              quint8  data[len];

          Let's consider the look-up of context "FunnyDialog".  A
          hash value between 0 and hTableSize - 1 is computed, say h.
          If hTable[h] is 0, "FunnyDialog" is not covered by this
          translator. Else, we check in the contextPool at offset
          2 * hTable[h] to see if "FunnyDialog" is one of the
          contexts stored there, until we find it or we meet the
          empty string.
        */
        m_contextArray.resize(2 + (hTableSize << 1));
        QDataStream t(&m_contextArray, QIODevice::WriteOnly);

        quint16 *hTable = new quint16[hTableSize];
        memset(hTable, 0, hTableSize * sizeof(quint16));

        t << hTableSize;
        t.device()->seek(2 + (hTableSize << 1));
        t << quint16(0); // the entry at offset 0 cannot be used
        uint upto = 2;

        auto entry = hashMap.constBegin();
        while (entry != hashMap.constEnd()) {
            int i = entry.key();
            hTable[i] = quint16(upto >> 1);

            do {
                const char *con = entry.value().constData();
                uint len = uint(entry.value().length());
                len = qMin(len, 255u);
                t << quint8(len);
                t.writeRawData(con, len);
                upto += 1 + len;
                ++entry;
            } while (entry != hashMap.constEnd() && entry.key() == i);
            if (upto & 0x1) {
                // offsets have to be even
                t << quint8(0); // empty string
                ++upto;
            }
        }
        t.device()->seek(2);
        for (int j = 0; j < hTableSize; j++)
            t << hTable[j];
        delete [] hTable;

        if (upto > 131072) {
            qWarning("Releaser::squeeze: Too many contexts");
            m_contextArray.clear();
        }
    }
}

void Releaser::insert(const TranslatorMessage &message, const QStringList &tlns, bool forceComment)
{
    ByteTranslatorMessage bmsg(originalBytes(message.context()),
                               originalBytes(message.sourceText()),
                               originalBytes(message.comment()),
                               tlns);
    if (!forceComment) {
        ByteTranslatorMessage bmsg2(
                bmsg.context(), bmsg.sourceText(), QByteArray(""), bmsg.translations());
        if (!m_messages.contains(bmsg2)) {
            m_messages.insert(bmsg2, 0);
            return;
        }
    }
    m_messages.insert(bmsg, 0);
}

void Releaser::insertIdBased(const TranslatorMessage &message, const QStringList &tlns)
{
    ByteTranslatorMessage bmsg("", originalBytes(message.id()), "", tlns);
    m_messages.insert(bmsg, 0);
}

void Releaser::setNumerusRules(const QByteArray &rules)
{
    m_numerusRules = rules;
}

void Releaser::setDependencies(const QStringList &dependencies)
{
    m_dependencies = dependencies;
}

static quint8 read8(const uchar *data)
{
    return *data;
}

static quint32 read32(const uchar *data)
{
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | (data[3]);
}

static void fromBytes(const char *str, int len, QString *out, bool *utf8Fail)
{
    QStringDecoder toUnicode(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *out = toUnicode(QByteArrayView(str, len));
    *utf8Fail = toUnicode.hasError();
}

bool loadQM(Translator &translator, QIODevice &dev, ConversionData &cd)
{
    QByteArray ba = dev.readAll();
    const uchar *data = (uchar*)ba.data();
    int len = ba.size();
    if (len < MagicLength || memcmp(data, magic, MagicLength) != 0) {
        cd.appendError(QLatin1String("QM-Format error: magic marker missing"));
        return false;
    }

    enum { Contexts = 0x2f, Hashes = 0x42, Messages = 0x69, NumerusRules = 0x88, Dependencies = 0x96, Language = 0xa7 };

    // for squeezed but non-file data, this is what needs to be deleted
    const uchar *messageArray = nullptr;
    const uchar *offsetArray = nullptr;
    uint offsetLength = 0;

    bool ok = true;
    bool utf8Fail = false;
    const uchar *end = data + len;

    data += MagicLength;

    while (data < end - 4) {
        quint8 tag = read8(data++);
        quint32 blockLen = read32(data);
        //qDebug() << "TAG:" << tag <<  "BLOCKLEN:" << blockLen;
        data += 4;
        if (!tag || !blockLen)
            break;
        if (data + blockLen > end) {
            ok = false;
            break;
        }

        if (tag == Hashes) {
            offsetArray = data;
            offsetLength = blockLen;
            //qDebug() << "HASHES: " << blockLen << QByteArray((const char *)data, blockLen).toHex();
        } else if (tag == Messages) {
            messageArray = data;
            //qDebug() << "MESSAGES: " << blockLen << QByteArray((const char *)data, blockLen).toHex();
        } else if (tag == Dependencies) {
            QStringList dependencies;
            QDataStream stream(QByteArray::fromRawData((const char*)data, blockLen));
            QString dep;
            while (!stream.atEnd()) {
                stream >> dep;
                dependencies.append(dep);
            }
            translator.setDependencies(dependencies);
        } else if (tag == Language) {
            QString language;
            fromBytes((const char *)data, blockLen, &language, &utf8Fail);
            translator.setLanguageCode(language);
        }

        data += blockLen;
    }


    size_t numItems = offsetLength / (2 * sizeof(quint32));
    //qDebug() << "NUMITEMS: " << numItems;

    QString strProN = QLatin1String("%n");
    QLocale::Language l;
    QLocale::Country c;
    Translator::languageAndCountry(translator.languageCode(), &l, &c);
    QStringList numerusForms;
    bool guessPlurals = true;
    if (getNumerusInfo(l, c, 0, &numerusForms, 0))
        guessPlurals = (numerusForms.count() == 1);

    QString context, sourcetext, comment;
    QStringList translations;

    for (const uchar *start = offsetArray; start != offsetArray + (numItems << 3); start += 8) {
        //quint32 hash = read32(start);
        quint32 ro = read32(start + 4);
        //qDebug() << "\nHASH:" << hash;
        const uchar *m = messageArray + ro;

        for (;;) {
            uchar tag = read8(m++);
            //qDebug() << "Tag:" << tag << " ADDR: " << m;
            switch(tag) {
            case Tag_End:
                goto end;
            case Tag_Translation: {
                int len = read32(m);
                m += 4;

                // -1 indicates an empty string
                // Otherwise streaming format is UTF-16 -> 2 bytes per character
                if ((len != -1) && (len & 1)) {
                    cd.appendError(QLatin1String("QM-Format error"));
                    return false;
                }
                QString str;
                if (len != -1)
                    str = QString((const QChar *)m, len / 2);
                if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
                    for (int i = 0; i < str.length(); ++i)
                        str[i] = QChar((str.at(i).unicode() >> 8) +
                            ((str.at(i).unicode() << 8) & 0xff00));
                }
                translations << str;
                m += len;
                break;
            }
            case Tag_Obsolete1:
                m += 4;
                //qDebug() << "OBSOLETE";
                break;
            case Tag_SourceText: {
                quint32 len = read32(m);
                m += 4;
                //qDebug() << "SOURCE LEN: " << len;
                //qDebug() << "SOURCE: " << QByteArray((const char*)m, len);
                fromBytes((const char*)m, len, &sourcetext, &utf8Fail);
                m += len;
                break;
            }
            case Tag_Context: {
                quint32 len = read32(m);
                m += 4;
                //qDebug() << "CONTEXT LEN: " << len;
                //qDebug() << "CONTEXT: " << QByteArray((const char*)m, len);
                fromBytes((const char*)m, len, &context, &utf8Fail);
                m += len;
                break;
            }
            case Tag_Comment: {
                quint32 len = read32(m);
                m += 4;
                //qDebug() << "COMMENT LEN: " << len;
                //qDebug() << "COMMENT: " << QByteArray((const char*)m, len);
                fromBytes((const char*)m, len, &comment, &utf8Fail);
                m += len;
                break;
            }
            default:
                //qDebug() << "UNKNOWN TAG" << tag;
                break;
            }
        }
    end:;
        TranslatorMessage msg;
        msg.setType(TranslatorMessage::Finished);
        if (translations.count() > 1) {
            // If guessPlurals is not false here, plural form discard messages
            // will be spewn out later.
            msg.setPlural(true);
        } else if (guessPlurals) {
            // This might cause false positives, so it is a fallback only.
            if (sourcetext.contains(strProN))
                msg.setPlural(true);
        }
        msg.setTranslations(translations);
        translations.clear();
        msg.setContext(context);
        msg.setSourceText(sourcetext);
        msg.setComment(comment);
        translator.append(msg);
    }
    if (utf8Fail) {
        cd.appendError(QLatin1String("Error: File contains invalid UTF-8 sequences."));
        return false;
    }
    return ok;
}



static bool containsStripped(const Translator &translator, const TranslatorMessage &msg)
{
    for (const TranslatorMessage &tmsg : translator.messages())
        if (tmsg.sourceText() == msg.sourceText()
            && tmsg.context() == msg.context()
            && tmsg.comment().isEmpty())
        return true;
    return false;
}

bool saveQM(const Translator &translator, QIODevice &dev, ConversionData &cd)
{
    Releaser releaser(translator.languageCode());
    QLocale::Language l;
    QLocale::Country c;
    Translator::languageAndCountry(translator.languageCode(), &l, &c);
    QByteArray rules;
    if (getNumerusInfo(l, c, &rules, 0, 0))
        releaser.setNumerusRules(rules);

    int finished = 0;
    int unfinished = 0;
    int untranslated = 0;
    int missingIds = 0;
    int droppedData = 0;

    for (int i = 0; i != translator.messageCount(); ++i) {
        const TranslatorMessage &msg = translator.message(i);
        TranslatorMessage::Type typ = msg.type();
        if (typ != TranslatorMessage::Obsolete && typ != TranslatorMessage::Vanished) {
            if (cd.m_idBased && msg.id().isEmpty()) {
                ++missingIds;
                continue;
            }
            if (typ == TranslatorMessage::Unfinished) {
                if (msg.translation().isEmpty() && !cd.m_idBased && cd.m_unTrPrefix.isEmpty()) {
                    ++untranslated;
                    continue;
                } else {
                    if (cd.ignoreUnfinished())
                        continue;
                    ++unfinished;
                }
            } else {
                ++finished;
            }
            QStringList tlns = msg.translations();
            if (msg.type() == TranslatorMessage::Unfinished
                && (cd.m_idBased || !cd.m_unTrPrefix.isEmpty()))
                for (int j = 0; j < tlns.size(); ++j)
                    if (tlns.at(j).isEmpty())
                        tlns[j] = cd.m_unTrPrefix + msg.sourceText();
            if (cd.m_idBased) {
                if (!msg.context().isEmpty() || !msg.comment().isEmpty())
                    ++droppedData;
                releaser.insertIdBased(msg, tlns);
            } else {
                // Drop the comment in (context, sourceText, comment),
                // unless the context is empty,
                // unless (context, sourceText, "") already exists or
                // unless we already dropped the comment of (context,
                // sourceText, comment0).
                bool forceComment =
                        msg.comment().isEmpty()
                        || msg.context().isEmpty()
                        || containsStripped(translator, msg);
                releaser.insert(msg, tlns, forceComment);
            }
        }
    }

    if (missingIds)
        cd.appendError(QCoreApplication::translate("LRelease",
            "Dropped %n message(s) which had no ID.", 0,
            missingIds));
    if (droppedData)
        cd.appendError(QCoreApplication::translate("LRelease",
            "Excess context/disambiguation dropped from %n message(s).", 0,
            droppedData));

    releaser.setDependencies(translator.dependencies());
    releaser.squeeze(cd.m_saveMode);
    bool saved = releaser.save(&dev);
    if (saved && cd.isVerbose()) {
        int generatedCount = finished + unfinished;
        cd.appendError(QCoreApplication::translate("LRelease",
            "    Generated %n translation(s) (%1 finished and %2 unfinished)", 0,
            generatedCount).arg(finished).arg(unfinished));
        if (untranslated)
            cd.appendError(QCoreApplication::translate("LRelease",
                "    Ignored %n untranslated source text(s)", 0,
                untranslated));
    }
    return saved;
}

int initQM()
{
    Translator::FileFormat format;

    format.extension = QLatin1String("qm");
    format.untranslatedDescription = QT_TRANSLATE_NOOP("FMT", "Compiled Qt translations");
    format.fileType = Translator::FileFormat::TranslationBinary;
    format.priority = 0;
    format.loader = &loadQM;
    format.saver = &saveQM;
    Translator::registerFileFormat(format);

    return 1;
}

Q_CONSTRUCTOR_FUNCTION(initQM)

QT_END_NAMESPACE
