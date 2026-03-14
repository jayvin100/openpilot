/********************************************************************************
** Form generated from reading UI file 'new_release_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_NEW_RELEASE_DIALOG_H
#define UI_NEW_RELEASE_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_NewReleaseDialog
{
public:
    QHBoxLayout *horizontalLayout_3;
    QLabel *label_3;
    QWidget *widget;
    QVBoxLayout *verticalLayout;
    QSpacerItem *verticalSpacer;
    QFormLayout *formLayout;
    QLabel *label_2;
    QLabel *labelRelease;
    QLabel *label_4;
    QLabel *labelTitle;
    QHBoxLayout *horizontalLayout;
    QPushButton *pushButtonWeb;
    QSpacerItem *verticalSpacer_2;
    QHBoxLayout *horizontalLayout_2;
    QCheckBox *dontShowAgain;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *NewReleaseDialog)
    {
        if (NewReleaseDialog->objectName().isEmpty())
            NewReleaseDialog->setObjectName(QString::fromUtf8("NewReleaseDialog"));
        NewReleaseDialog->resize(725, 300);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(NewReleaseDialog->sizePolicy().hasHeightForWidth());
        NewReleaseDialog->setSizePolicy(sizePolicy);
        NewReleaseDialog->setMinimumSize(QSize(725, 300));
        NewReleaseDialog->setMaximumSize(QSize(725, 300));
        horizontalLayout_3 = new QHBoxLayout(NewReleaseDialog);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        label_3 = new QLabel(NewReleaseDialog);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setMinimumSize(QSize(350, 250));
        label_3->setMaximumSize(QSize(350, 250));
        label_3->setFrameShape(QFrame::Box);
        label_3->setPixmap(QPixmap(QString::fromUtf8(":/resources/update_plotjuggler.jpg")));

        horizontalLayout_3->addWidget(label_3);

        widget = new QWidget(NewReleaseDialog);
        widget->setObjectName(QString::fromUtf8("widget"));
        widget->setMinimumSize(QSize(350, 0));
        widget->setMaximumSize(QSize(350, 16777215));
        verticalLayout = new QVBoxLayout(widget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setHorizontalSpacing(12);
        formLayout->setVerticalSpacing(12);
        formLayout->setContentsMargins(5, -1, -1, -1);
        label_2 = new QLabel(widget);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        label_2->setFont(font);

        formLayout->setWidget(0, QFormLayout::LabelRole, label_2);

        labelRelease = new QLabel(widget);
        labelRelease->setObjectName(QString::fromUtf8("labelRelease"));
        labelRelease->setWordWrap(true);

        formLayout->setWidget(0, QFormLayout::FieldRole, labelRelease);

        label_4 = new QLabel(widget);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setFont(font);

        formLayout->setWidget(1, QFormLayout::LabelRole, label_4);

        labelTitle = new QLabel(widget);
        labelTitle->setObjectName(QString::fromUtf8("labelTitle"));
        labelTitle->setWordWrap(true);

        formLayout->setWidget(1, QFormLayout::FieldRole, labelTitle);


        verticalLayout->addLayout(formLayout);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        pushButtonWeb = new QPushButton(widget);
        pushButtonWeb->setObjectName(QString::fromUtf8("pushButtonWeb"));
        pushButtonWeb->setMaximumSize(QSize(120, 16777215));
        QFont font1;
        font1.setPointSize(13);
        pushButtonWeb->setFont(font1);
        pushButtonWeb->setFocusPolicy(Qt::NoFocus);

        horizontalLayout->addWidget(pushButtonWeb);


        verticalLayout->addLayout(horizontalLayout);

        verticalSpacer_2 = new QSpacerItem(20, 86, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer_2);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        dontShowAgain = new QCheckBox(widget);
        dontShowAgain->setObjectName(QString::fromUtf8("dontShowAgain"));
        dontShowAgain->setMinimumSize(QSize(150, 0));
        dontShowAgain->setMaximumSize(QSize(170, 16777215));
        dontShowAgain->setFocusPolicy(Qt::NoFocus);

        horizontalLayout_2->addWidget(dontShowAgain);


        verticalLayout->addLayout(horizontalLayout_2);

        buttonBox = new QDialogButtonBox(widget);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout->addWidget(buttonBox);


        horizontalLayout_3->addWidget(widget);


        retranslateUi(NewReleaseDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), NewReleaseDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), NewReleaseDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(NewReleaseDialog);
    } // setupUi

    void retranslateUi(QDialog *NewReleaseDialog)
    {
        NewReleaseDialog->setWindowTitle(QCoreApplication::translate("NewReleaseDialog", "New version available", nullptr));
        label_3->setText(QString());
        label_2->setText(QCoreApplication::translate("NewReleaseDialog", "Release: ", nullptr));
        labelRelease->setText(QCoreApplication::translate("NewReleaseDialog", "TextLabel", nullptr));
        label_4->setText(QCoreApplication::translate("NewReleaseDialog", "Title:", nullptr));
        labelTitle->setText(QCoreApplication::translate("NewReleaseDialog", "TextLabel", nullptr));
        pushButtonWeb->setText(QCoreApplication::translate("NewReleaseDialog", "Tell me more", nullptr));
        dontShowAgain->setText(QCoreApplication::translate("NewReleaseDialog", "Don't show me again", nullptr));
    } // retranslateUi

};

namespace Ui {
    class NewReleaseDialog: public Ui_NewReleaseDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_NEW_RELEASE_DIALOG_H
