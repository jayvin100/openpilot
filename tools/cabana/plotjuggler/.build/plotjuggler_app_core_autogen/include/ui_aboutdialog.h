/********************************************************************************
** Form generated from reading UI file 'aboutdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ABOUTDIALOG_H
#define UI_ABOUTDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_AboutDialog
{
public:
    QVBoxLayout *verticalLayout;
    QTextBrowser *titleTextBrowser;
    QLabel *label_version;
    QTextBrowser *bodyTextBrowser;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *AboutDialog)
    {
        if (AboutDialog->objectName().isEmpty())
            AboutDialog->setObjectName(QString::fromUtf8("AboutDialog"));
        AboutDialog->resize(800, 490);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(AboutDialog->sizePolicy().hasHeightForWidth());
        AboutDialog->setSizePolicy(sizePolicy);
        AboutDialog->setMinimumSize(QSize(800, 420));
        AboutDialog->setMaximumSize(QSize(800, 500));
        AboutDialog->setStyleSheet(QString::fromUtf8(""));
        verticalLayout = new QVBoxLayout(AboutDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(20, -1, 20, -1);
        titleTextBrowser = new QTextBrowser(AboutDialog);
        titleTextBrowser->setObjectName(QString::fromUtf8("titleTextBrowser"));
        titleTextBrowser->setMinimumSize(QSize(0, 50));
        titleTextBrowser->setMaximumSize(QSize(16777215, 100));
        titleTextBrowser->setStyleSheet(QString::fromUtf8("background-color: rgb(255,255,255,0);\n"
"border-color: rgb(255, 255, 255,0);"));
        titleTextBrowser->setFrameShape(QFrame::NoFrame);
        titleTextBrowser->setFrameShadow(QFrame::Plain);
        titleTextBrowser->setLineWidth(0);

        verticalLayout->addWidget(titleTextBrowser);

        label_version = new QLabel(AboutDialog);
        label_version->setObjectName(QString::fromUtf8("label_version"));
        label_version->setMinimumSize(QSize(0, 60));
        QFont font;
        font.setPointSize(16);
        label_version->setFont(font);
        label_version->setStyleSheet(QString::fromUtf8("background-color: rgb(255,255,255,0);\n"
"border-color: rgb(255, 255, 255,0);"));
        label_version->setAlignment(Qt::AlignHCenter|Qt::AlignTop);

        verticalLayout->addWidget(label_version);

        bodyTextBrowser = new QTextBrowser(AboutDialog);
        bodyTextBrowser->setObjectName(QString::fromUtf8("bodyTextBrowser"));
        bodyTextBrowser->setStyleSheet(QString::fromUtf8("background-color: rgb(255,255,255,0);\n"
"border-color: rgb(255, 255, 255,0);"));
        bodyTextBrowser->setFrameShape(QFrame::NoFrame);
        bodyTextBrowser->setFrameShadow(QFrame::Plain);
        bodyTextBrowser->setLineWidth(0);
        bodyTextBrowser->setReadOnly(true);
        bodyTextBrowser->setOpenExternalLinks(true);

        verticalLayout->addWidget(bodyTextBrowser);

        buttonBox = new QDialogButtonBox(AboutDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(AboutDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), AboutDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), AboutDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(AboutDialog);
    } // setupUi

    void retranslateUi(QDialog *AboutDialog)
    {
        AboutDialog->setWindowTitle(QCoreApplication::translate("AboutDialog", "About PlotJuggler", nullptr));
        titleTextBrowser->setHtml(QCoreApplication::translate("AboutDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p align=\"center\" style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p align=\"center\" style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Ubuntu-light'; font-size:26pt; color:#ce0e73;\">Plot</span><span style=\" font-family:'Ubuntu-light'; font-size:26pt; color:#1b72cf;\">Juggler</span></p></body></html>", nullptr));
        label_version->setText(QCoreApplication::translate("AboutDialog", "version:", nullptr));
        bodyTextBrowser->setHtml(QCoreApplication::translate("AboutDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Sans Serif'; font-weight:600;\">PlotJuggler</span><span style=\" font-family:'Sans Serif'; font-weight:296;\"> was built with love by </span><span style=\" font-family:'Sans Serif'; font-weight:600;\">Davide Faconti</span><span style=\" font-family:'Sans Serif'; font-weight:296;\">, see </span><a href=\"https://www.plotjuggler.io\"><span style=\" font-family:'Sans Serif'; font-weight:600; text-decoration: underline; color:#f704f8;\">www.plotjuggler.io</span></a><span style=\" font-family:'Sans Serif'; font-weight:296;\"> </span></p>\n"
""
                        "<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Sans Serif'; font-weight:296;\">The official GitHub repository is:: </span><a href=\"https://github.com/facontidavide/PlotJuggler\"><span style=\" font-family:'Sans Serif'; font-weight:600; text-decoration: underline; color:#f704f8;\">https://github.com/facontidavide/PlotJuggler</span></a><span style=\" font-family:'Sans Serif'; font-weight:296;\"> </span></p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Sans Serif'; font-weight:296;\">Use GitHub issues to report bugs and suggest new features. </span></p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Sans Serif'; font-weight:296;\">If you need commercial support and custom development of new featu"
                        "res, please contact met at: </span></p>\n"
"<p align=\"center\" style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-family:'Sans Serif'; font-weight:600; color:#f704f8;\">davide.faconti@gmail.com</span></p></body></html>", nullptr));
    } // retranslateUi

};

namespace Ui {
    class AboutDialog: public Ui_AboutDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ABOUTDIALOG_H
