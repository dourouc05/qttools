// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0


#include "translationsettingsdialog.h"
#include "messagemodel.h"
#include "phrase.h"

#include <QtCore/QLocale>

QT_BEGIN_NAMESPACE

TranslationSettingsDialog::TranslationSettingsDialog(QWidget *parent)
  : QDialog(parent)
{
    m_ui.setupUi(this);

    for (int i = QLocale::C + 1; i < QLocale::LastLanguage; ++i) {
        const auto language = QLocale::Language(i);
        QString lang = QLocale::languageToString(language);
        const auto loc = QLocale(language);
        // Languages for which we have no data get mapped to the default locale;
        // its endonym is unrelated to the language requested. For English, the
        // endonym is the name we already have; don't repeat it.
        if (loc.language() == language && language != QLocale::English) {
            const QString native = loc.nativeLanguageName();
            if (!native.isEmpty()) //: <english> (<endonym>)  (language names)
                lang = tr("%1 (%2)").arg(lang, native);
        }
        m_ui.srcCbLanguageList->addItem(lang, QVariant(i));
    }
    m_ui.srcCbLanguageList->model()->sort(0, Qt::AscendingOrder);
    m_ui.srcCbLanguageList->insertItem(0, QLatin1String("POSIX"), QVariant(QLocale::C));

    m_ui.tgtCbLanguageList->setModel(m_ui.srcCbLanguageList->model());
}

void TranslationSettingsDialog::setDataModel(DataModel *dataModel)
{
    m_dataModel = dataModel;
    m_phraseBook = 0;
    QString fn = QFileInfo(dataModel->srcFileName()).baseName();
    setWindowTitle(tr("Settings for '%1' - Qt Linguist").arg(fn));
}

void TranslationSettingsDialog::setPhraseBook(PhraseBook *phraseBook)
{
    m_phraseBook = phraseBook;
    m_dataModel = 0;
    QString fn = QFileInfo(phraseBook->fileName()).baseName();
    setWindowTitle(tr("Settings for '%1' - Qt Linguist").arg(fn));
}

static void fillCountryCombo(const QVariant &lng, QComboBox *combo)
{
    combo->clear();
    QLocale::Language lang = QLocale::Language(lng.toInt());
    if (lang != QLocale::C) {
        for (QLocale::Country cntr : QLocale::countriesForLanguage(lang)) {
            QString country = QLocale::countryToString(cntr);
            auto loc = QLocale(lang, cntr);
            if (loc.language() != QLocale::English) {
                QString ncn = loc.nativeCountryName();
                if (!ncn.isEmpty())
                    country = TranslationSettingsDialog::tr("%1 (%2)").arg(country, ncn);
            }
            combo->addItem(country, QVariant(cntr));
        }
        combo->model()->sort(0, Qt::AscendingOrder);
    }
    combo->insertItem(0, TranslationSettingsDialog::tr("Any Country"), QVariant(QLocale::AnyCountry));
    combo->setCurrentIndex(0);
}

void TranslationSettingsDialog::on_srcCbLanguageList_currentIndexChanged(int idx)
{
    fillCountryCombo(m_ui.srcCbLanguageList->itemData(idx), m_ui.srcCbCountryList);
}

void TranslationSettingsDialog::on_tgtCbLanguageList_currentIndexChanged(int idx)
{
    fillCountryCombo(m_ui.tgtCbLanguageList->itemData(idx), m_ui.tgtCbCountryList);
}

void TranslationSettingsDialog::on_buttonBox_accepted()
{
    int itemindex = m_ui.tgtCbLanguageList->currentIndex();
    QVariant var = m_ui.tgtCbLanguageList->itemData(itemindex);
    QLocale::Language lang = QLocale::Language(var.toInt());

    itemindex = m_ui.tgtCbCountryList->currentIndex();
    var = m_ui.tgtCbCountryList->itemData(itemindex);
    QLocale::Country country = QLocale::Country(var.toInt());

    itemindex = m_ui.srcCbLanguageList->currentIndex();
    var = m_ui.srcCbLanguageList->itemData(itemindex);
    QLocale::Language lang2 = QLocale::Language(var.toInt());

    itemindex = m_ui.srcCbCountryList->currentIndex();
    var = m_ui.srcCbCountryList->itemData(itemindex);
    QLocale::Country country2 = QLocale::Country(var.toInt());

    if (m_phraseBook) {
        m_phraseBook->setLanguageAndCountry(lang, country);
        m_phraseBook->setSourceLanguageAndCountry(lang2, country2);
    } else {
        m_dataModel->setLanguageAndCountry(lang, country);
        m_dataModel->setSourceLanguageAndCountry(lang2, country2);
    }

    accept();
}

void TranslationSettingsDialog::showEvent(QShowEvent *)
{
    QLocale::Language lang, lang2;
    QLocale::Country country, country2;

    if (m_phraseBook) {
        lang = m_phraseBook->language();
        country = m_phraseBook->country();
        lang2 = m_phraseBook->sourceLanguage();
        country2 = m_phraseBook->sourceCountry();
    } else {
        lang = m_dataModel->language();
        country = m_dataModel->country();
        lang2 = m_dataModel->sourceLanguage();
        country2 = m_dataModel->sourceCountry();
    }

    int itemindex = m_ui.tgtCbLanguageList->findData(QVariant(int(lang)));
    m_ui.tgtCbLanguageList->setCurrentIndex(itemindex == -1 ? 0 : itemindex);

    itemindex = m_ui.tgtCbCountryList->findData(QVariant(int(country)));
    m_ui.tgtCbCountryList->setCurrentIndex(itemindex == -1 ? 0 : itemindex);

    itemindex = m_ui.srcCbLanguageList->findData(QVariant(int(lang2)));
    m_ui.srcCbLanguageList->setCurrentIndex(itemindex == -1 ? 0 : itemindex);

    itemindex = m_ui.srcCbCountryList->findData(QVariant(int(country2)));
    m_ui.srcCbCountryList->setCurrentIndex(itemindex == -1 ? 0 : itemindex);
}

QT_END_NAMESPACE
