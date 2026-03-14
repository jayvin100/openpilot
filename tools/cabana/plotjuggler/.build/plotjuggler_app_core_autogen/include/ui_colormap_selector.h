/********************************************************************************
** Form generated from reading UI file 'colormap_selector.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_COLORMAP_SELECTOR_H
#define UI_COLORMAP_SELECTOR_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_colormap_selector
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label_3;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *lineSeries;
    QLabel *label_2;
    QComboBox *comboBox;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer;
    QPushButton *buttonEditor;
    QSpacerItem *horizontalSpacer_2;
    QLabel *label_4;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *colormap_selector)
    {
        if (colormap_selector->objectName().isEmpty())
            colormap_selector->setObjectName(QString::fromUtf8("colormap_selector"));
        colormap_selector->resize(427, 304);
        verticalLayout = new QVBoxLayout(colormap_selector);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label_3 = new QLabel(colormap_selector);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setWordWrap(true);

        verticalLayout->addWidget(label_3);

        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        label = new QLabel(colormap_selector);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        lineSeries = new QLineEdit(colormap_selector);
        lineSeries->setObjectName(QString::fromUtf8("lineSeries"));
        lineSeries->setMinimumSize(QSize(0, 30));
        lineSeries->setReadOnly(true);

        formLayout->setWidget(0, QFormLayout::FieldRole, lineSeries);

        label_2 = new QLabel(colormap_selector);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        comboBox = new QComboBox(colormap_selector);
        comboBox->setObjectName(QString::fromUtf8("comboBox"));
        comboBox->setMinimumSize(QSize(0, 30));

        formLayout->setWidget(1, QFormLayout::FieldRole, comboBox);


        verticalLayout->addLayout(formLayout);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(-1, 22, 0, 22);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        buttonEditor = new QPushButton(colormap_selector);
        buttonEditor->setObjectName(QString::fromUtf8("buttonEditor"));

        horizontalLayout->addWidget(buttonEditor);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_2);


        verticalLayout->addLayout(horizontalLayout);

        label_4 = new QLabel(colormap_selector);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        verticalLayout->addWidget(label_4);

        buttonBox = new QDialogButtonBox(colormap_selector);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok|QDialogButtonBox::Reset);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(colormap_selector);
        QObject::connect(buttonBox, SIGNAL(accepted()), colormap_selector, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), colormap_selector, SLOT(reject()));

        QMetaObject::connectSlotsByName(colormap_selector);
    } // setupUi

    void retranslateUi(QDialog *colormap_selector)
    {
        colormap_selector->setWindowTitle(QCoreApplication::translate("colormap_selector", "Background Color", nullptr));
        label_3->setText(QCoreApplication::translate("colormap_selector", "Change the color of the plot background using a ColorMap associated to a particular timeseries.", nullptr));
        label->setText(QCoreApplication::translate("colormap_selector", "Reference Timeseries:", nullptr));
        label_2->setText(QCoreApplication::translate("colormap_selector", "Selecteded ColorMap", nullptr));
        buttonEditor->setText(QCoreApplication::translate("colormap_selector", "ColorMap Editor", nullptr));
        label_4->setText(QCoreApplication::translate("colormap_selector", "Press \"Reset\" to remove the background.", nullptr));
    } // retranslateUi

};

namespace Ui {
    class colormap_selector: public Ui_colormap_selector {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_COLORMAP_SELECTOR_H
