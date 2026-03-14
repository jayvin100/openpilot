/********************************************************************************
** Form generated from reading UI file 'transform_selector.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_TRANSFORM_SELECTOR_H
#define UI_TRANSFORM_SELECTOR_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_transform_selector
{
public:
    QVBoxLayout *verticalLayout_2;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QComboBox *comboDefault;
    QHBoxLayout *horizontalLayout_2;
    QPushButton *buttonApplyDefault;
    QPushButton *buttonResetAll;
    QSpacerItem *horizontalSpacer;
    QTableWidget *tableWidget;
    QLabel *label_2;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *transform_selector)
    {
        if (transform_selector->objectName().isEmpty())
            transform_selector->setObjectName(QString::fromUtf8("transform_selector"));
        transform_selector->resize(591, 409);
        verticalLayout_2 = new QVBoxLayout(transform_selector);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label = new QLabel(transform_selector);
        label->setObjectName(QString::fromUtf8("label"));

        horizontalLayout->addWidget(label);

        comboDefault = new QComboBox(transform_selector);
        comboDefault->setObjectName(QString::fromUtf8("comboDefault"));
        comboDefault->setMinimumSize(QSize(200, 0));

        horizontalLayout->addWidget(comboDefault);


        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        buttonApplyDefault = new QPushButton(transform_selector);
        buttonApplyDefault->setObjectName(QString::fromUtf8("buttonApplyDefault"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(buttonApplyDefault->sizePolicy().hasHeightForWidth());
        buttonApplyDefault->setSizePolicy(sizePolicy);
        buttonApplyDefault->setFocusPolicy(Qt::NoFocus);

        horizontalLayout_2->addWidget(buttonApplyDefault);

        buttonResetAll = new QPushButton(transform_selector);
        buttonResetAll->setObjectName(QString::fromUtf8("buttonResetAll"));
        sizePolicy.setHeightForWidth(buttonResetAll->sizePolicy().hasHeightForWidth());
        buttonResetAll->setSizePolicy(sizePolicy);
        buttonResetAll->setFocusPolicy(Qt::NoFocus);

        horizontalLayout_2->addWidget(buttonResetAll);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer);


        verticalLayout->addLayout(horizontalLayout_2);

        tableWidget = new QTableWidget(transform_selector);
        tableWidget->setObjectName(QString::fromUtf8("tableWidget"));
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
        tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableWidget->horizontalHeader()->setVisible(false);
        tableWidget->horizontalHeader()->setProperty("showSortIndicator", QVariant(true));
        tableWidget->horizontalHeader()->setStretchLastSection(true);
        tableWidget->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableWidget);

        label_2 = new QLabel(transform_selector);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        verticalLayout->addWidget(label_2);

        buttonBox = new QDialogButtonBox(transform_selector);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        verticalLayout_2->addLayout(verticalLayout);


        retranslateUi(transform_selector);
        QObject::connect(buttonBox, SIGNAL(accepted()), transform_selector, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), transform_selector, SLOT(reject()));

        QMetaObject::connectSlotsByName(transform_selector);
    } // setupUi

    void retranslateUi(QDialog *transform_selector)
    {
        transform_selector->setWindowTitle(QCoreApplication::translate("transform_selector", "Dialog", nullptr));
        label->setText(QCoreApplication::translate("transform_selector", "Default (for newly added timeseries):", nullptr));
        buttonApplyDefault->setText(QCoreApplication::translate("transform_selector", "All to default", nullptr));
        buttonResetAll->setText(QCoreApplication::translate("transform_selector", "Reset all", nullptr));
        label_2->setText(QCoreApplication::translate("transform_selector", "Note: invalid transforms will not be displayed", nullptr));
    } // retranslateUi

};

namespace Ui {
    class transform_selector: public Ui_transform_selector {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_TRANSFORM_SELECTOR_H
