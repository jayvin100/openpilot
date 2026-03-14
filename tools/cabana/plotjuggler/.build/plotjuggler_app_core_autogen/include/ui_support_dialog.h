/********************************************************************************
** Form generated from reading UI file 'support_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SUPPORT_DIALOG_H
#define UI_SUPPORT_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_SupportDialog
{
public:
    QVBoxLayout *verticalLayout_2;
    QHBoxLayout *horizontalLayout;
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QTextBrowser *textBrowser;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *SupportDialog)
    {
        if (SupportDialog->objectName().isEmpty())
            SupportDialog->setObjectName(QString::fromUtf8("SupportDialog"));
        SupportDialog->resize(700, 450);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(SupportDialog->sizePolicy().hasHeightForWidth());
        SupportDialog->setSizePolicy(sizePolicy);
        SupportDialog->setMinimumSize(QSize(700, 450));
        SupportDialog->setMaximumSize(QSize(700, 450));
        SupportDialog->setStyleSheet(QString::fromUtf8("background: white"));
        verticalLayout_2 = new QVBoxLayout(SupportDialog);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(20);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(SupportDialog);
        label->setObjectName(QString::fromUtf8("label"));
        label->setMinimumSize(QSize(200, 210));
        label->setMaximumSize(QSize(200, 210));
        label->setPixmap(QPixmap(QString::fromUtf8(":/resources/Davide.jpg")));
        label->setScaledContents(true);
        label->setAlignment(Qt::AlignHCenter|Qt::AlignTop);

        verticalLayout->addWidget(label);


        horizontalLayout->addLayout(verticalLayout);

        textBrowser = new QTextBrowser(SupportDialog);
        textBrowser->setObjectName(QString::fromUtf8("textBrowser"));
        textBrowser->setFrameShape(QFrame::NoFrame);
        textBrowser->setFrameShadow(QFrame::Plain);
        textBrowser->setLineWidth(0);
        textBrowser->setOpenExternalLinks(true);

        horizontalLayout->addWidget(textBrowser);


        verticalLayout_2->addLayout(horizontalLayout);

        buttonBox = new QDialogButtonBox(SupportDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout_2->addWidget(buttonBox);


        retranslateUi(SupportDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), SupportDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), SupportDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(SupportDialog);
    } // setupUi

    void retranslateUi(QDialog *SupportDialog)
    {
        SupportDialog->setWindowTitle(QCoreApplication::translate("SupportDialog", "Support", nullptr));
        label->setText(QString());
        textBrowser->setHtml(QCoreApplication::translate("SupportDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Hi!</p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">I am Davide, the creator of<span style=\" font-weight:600;\"> </span><span style=\" font-weight:600; color:#ff00ff;\">Plot</span><span style=\" font-weight:600; color:#0000ff;\">Juggler.</span> I hope you find this software useful!</p>\n"
"<p style=\" margin-top:12px; mar"
                        "gin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">I have been developing PlotJuggler in my spare time since 2016,  because I wanted to build the best tool to visualize logs, traces and data.</p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">If you are an individual, consider making a <span style=\" font-weight:600;\">donation</span> with <a href=\"https://www.paypal.me/facontidavide\"><span style=\" text-decoration: underline; color:#0000ff;\">PayPal.</span></a></p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">If you use PlotJuggler at <span style=\" font-weight:600;\">work</span>, your company can become a <span style=\" font-weight:600;\">sponsor</span> or support the development of a specific feature.</p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-inden"
                        "t:0; text-indent:0px;\"><a href=\"mailto:davide.faconti@gmail.com\"><span style=\" text-decoration: underline; color:#0000ff;\">Contact me</span></a> for more details.</p>\n"
"<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"> </p></body></html>", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SupportDialog: public Ui_SupportDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SUPPORT_DIALOG_H
