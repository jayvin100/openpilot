/********************************************************************************
** Form generated from reading UI file 'outlier_removal.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_OUTLIER_REMOVAL_H
#define UI_OUTLIER_REMOVAL_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_OutlierRemovalFilter
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QLabel *label_3;
    QFormLayout *formLayout;
    QLabel *label_2;
    QDoubleSpinBox *spinBoxFactor;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *OutlierRemovalFilter)
    {
        if (OutlierRemovalFilter->objectName().isEmpty())
            OutlierRemovalFilter->setObjectName(QString::fromUtf8("OutlierRemovalFilter"));
        OutlierRemovalFilter->resize(400, 300);
        verticalLayout = new QVBoxLayout(OutlierRemovalFilter);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(OutlierRemovalFilter);
        label->setObjectName(QString::fromUtf8("label"));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        label->setFont(font);
        label->setWordWrap(true);

        verticalLayout->addWidget(label);

        label_3 = new QLabel(OutlierRemovalFilter);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setWordWrap(true);

        verticalLayout->addWidget(label_3);

        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setContentsMargins(-1, 10, -1, -1);
        label_2 = new QLabel(OutlierRemovalFilter);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label_2);

        spinBoxFactor = new QDoubleSpinBox(OutlierRemovalFilter);
        spinBoxFactor->setObjectName(QString::fromUtf8("spinBoxFactor"));
        spinBoxFactor->setMaximumSize(QSize(120, 16777215));
        spinBoxFactor->setDecimals(1);
        spinBoxFactor->setMinimum(10.000000000000000);
        spinBoxFactor->setMaximum(100000.000000000000000);
        spinBoxFactor->setValue(100.000000000000000);

        formLayout->setWidget(0, QFormLayout::FieldRole, spinBoxFactor);


        verticalLayout->addLayout(formLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(OutlierRemovalFilter);

        QMetaObject::connectSlotsByName(OutlierRemovalFilter);
    } // setupUi

    void retranslateUi(QWidget *OutlierRemovalFilter)
    {
        OutlierRemovalFilter->setWindowTitle(QCoreApplication::translate("OutlierRemovalFilter", "Form", nullptr));
        label->setText(QCoreApplication::translate("OutlierRemovalFilter", "Remove outliers:", nullptr));
        label_3->setText(QCoreApplication::translate("OutlierRemovalFilter", "Outlier are detected using the difference between the current 1st order derivative and the previous one.", nullptr));
        label_2->setText(QCoreApplication::translate("OutlierRemovalFilter", "Max ratio", nullptr));
    } // retranslateUi

};

namespace Ui {
    class OutlierRemovalFilter: public Ui_OutlierRemovalFilter {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_OUTLIER_REMOVAL_H
