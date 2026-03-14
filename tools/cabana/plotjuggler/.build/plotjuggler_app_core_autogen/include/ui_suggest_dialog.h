/********************************************************************************
** Form generated from reading UI file 'suggest_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SUGGEST_DIALOG_H
#define UI_SUGGEST_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_SuggestDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label_2;
    QLabel *label_3;
    QLineEdit *lineEditX;
    QLineEdit *lineEditY;
    QPushButton *pushButtonSwap;
    QFrame *line;
    QLabel *label;
    QLineEdit *lineEditName;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *SuggestDialog)
    {
        if (SuggestDialog->objectName().isEmpty())
            SuggestDialog->setObjectName(QString::fromUtf8("SuggestDialog"));
        SuggestDialog->resize(525, 217);
        verticalLayout = new QVBoxLayout(SuggestDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setContentsMargins(-1, 11, -1, -1);
        label_2 = new QLabel(SuggestDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label_2);

        label_3 = new QLabel(SuggestDialog);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_3);

        lineEditX = new QLineEdit(SuggestDialog);
        lineEditX->setObjectName(QString::fromUtf8("lineEditX"));
        lineEditX->setReadOnly(true);

        formLayout->setWidget(0, QFormLayout::FieldRole, lineEditX);

        lineEditY = new QLineEdit(SuggestDialog);
        lineEditY->setObjectName(QString::fromUtf8("lineEditY"));
        lineEditY->setReadOnly(true);

        formLayout->setWidget(1, QFormLayout::FieldRole, lineEditY);

        pushButtonSwap = new QPushButton(SuggestDialog);
        pushButtonSwap->setObjectName(QString::fromUtf8("pushButtonSwap"));

        formLayout->setWidget(2, QFormLayout::LabelRole, pushButtonSwap);


        verticalLayout->addLayout(formLayout);

        line = new QFrame(SuggestDialog);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        verticalLayout->addWidget(line);

        label = new QLabel(SuggestDialog);
        label->setObjectName(QString::fromUtf8("label"));

        verticalLayout->addWidget(label);

        lineEditName = new QLineEdit(SuggestDialog);
        lineEditName->setObjectName(QString::fromUtf8("lineEditName"));

        verticalLayout->addWidget(lineEditName);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(SuggestDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(SuggestDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), SuggestDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), SuggestDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(SuggestDialog);
    } // setupUi

    void retranslateUi(QDialog *SuggestDialog)
    {
        SuggestDialog->setWindowTitle(QCoreApplication::translate("SuggestDialog", "Nex XY Curve", nullptr));
        label_2->setText(QCoreApplication::translate("SuggestDialog", "X", nullptr));
        label_3->setText(QCoreApplication::translate("SuggestDialog", "Y", nullptr));
        pushButtonSwap->setText(QCoreApplication::translate("SuggestDialog", "Swap", nullptr));
        label->setText(QCoreApplication::translate("SuggestDialog", "The XY curve needs a custom name:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SuggestDialog: public Ui_SuggestDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SUGGEST_DIALOG_H
