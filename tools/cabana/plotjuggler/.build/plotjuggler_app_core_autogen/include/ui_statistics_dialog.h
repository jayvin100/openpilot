/********************************************************************************
** Form generated from reading UI file 'statistics_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_STATISTICS_DIALOG_H
#define UI_STATISTICS_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_statistics_dialog
{
public:
    QVBoxLayout *verticalLayout;
    QTableWidget *tableWidget;
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QComboBox *rangeComboBox;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *statistics_dialog)
    {
        if (statistics_dialog->objectName().isEmpty())
            statistics_dialog->setObjectName(QString::fromUtf8("statistics_dialog"));
        statistics_dialog->resize(1007, 595);
        verticalLayout = new QVBoxLayout(statistics_dialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        tableWidget = new QTableWidget(statistics_dialog);
        if (tableWidget->columnCount() < 5)
            tableWidget->setColumnCount(5);
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(0, __qtablewidgetitem);
        QTableWidgetItem *__qtablewidgetitem1 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(1, __qtablewidgetitem1);
        QTableWidgetItem *__qtablewidgetitem2 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(2, __qtablewidgetitem2);
        QTableWidgetItem *__qtablewidgetitem3 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(3, __qtablewidgetitem3);
        QTableWidgetItem *__qtablewidgetitem4 = new QTableWidgetItem();
        tableWidget->setHorizontalHeaderItem(4, __qtablewidgetitem4);
        tableWidget->setObjectName(QString::fromUtf8("tableWidget"));
        tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
        tableWidget->setSortingEnabled(false);
        tableWidget->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableWidget);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label = new QLabel(statistics_dialog);
        label->setObjectName(QString::fromUtf8("label"));
        QFont font;
        font.setFamily(QString::fromUtf8("Segoe UI"));
        font.setPointSize(9);
        label->setFont(font);

        horizontalLayout->addWidget(label);

        rangeComboBox = new QComboBox(statistics_dialog);
        rangeComboBox->addItem(QString());
        rangeComboBox->addItem(QString());
        rangeComboBox->setObjectName(QString::fromUtf8("rangeComboBox"));
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(rangeComboBox->sizePolicy().hasHeightForWidth());
        rangeComboBox->setSizePolicy(sizePolicy);
        rangeComboBox->setMinimumSize(QSize(125, 0));
        rangeComboBox->setFont(font);

        horizontalLayout->addWidget(rangeComboBox);

        buttonBox = new QDialogButtonBox(statistics_dialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setFont(font);
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        horizontalLayout->addWidget(buttonBox);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(statistics_dialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), statistics_dialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), statistics_dialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(statistics_dialog);
    } // setupUi

    void retranslateUi(QDialog *statistics_dialog)
    {
        statistics_dialog->setWindowTitle(QCoreApplication::translate("statistics_dialog", "Statistics", nullptr));
        QTableWidgetItem *___qtablewidgetitem = tableWidget->horizontalHeaderItem(0);
        ___qtablewidgetitem->setText(QCoreApplication::translate("statistics_dialog", "Series name", nullptr));
        QTableWidgetItem *___qtablewidgetitem1 = tableWidget->horizontalHeaderItem(1);
        ___qtablewidgetitem1->setText(QCoreApplication::translate("statistics_dialog", "Samples Count", nullptr));
        QTableWidgetItem *___qtablewidgetitem2 = tableWidget->horizontalHeaderItem(2);
        ___qtablewidgetitem2->setText(QCoreApplication::translate("statistics_dialog", "Minimum", nullptr));
        QTableWidgetItem *___qtablewidgetitem3 = tableWidget->horizontalHeaderItem(3);
        ___qtablewidgetitem3->setText(QCoreApplication::translate("statistics_dialog", "Maximum", nullptr));
        QTableWidgetItem *___qtablewidgetitem4 = tableWidget->horizontalHeaderItem(4);
        ___qtablewidgetitem4->setText(QCoreApplication::translate("statistics_dialog", "Average", nullptr));
        label->setText(QCoreApplication::translate("statistics_dialog", "Calculate statistics from: ", nullptr));
        rangeComboBox->setItemText(0, QCoreApplication::translate("statistics_dialog", "Visible Range", nullptr));
        rangeComboBox->setItemText(1, QCoreApplication::translate("statistics_dialog", "Full Range", nullptr));

    } // retranslateUi

};

namespace Ui {
    class statistics_dialog: public Ui_statistics_dialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_STATISTICS_DIALOG_H
