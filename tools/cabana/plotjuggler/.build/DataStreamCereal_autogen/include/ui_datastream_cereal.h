/********************************************************************************
** Form generated from reading UI file 'datastream_cereal.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_DATASTREAM_CEREAL_H
#define UI_DATASTREAM_CEREAL_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_DataStreamCereal
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QGridLayout *gridLayout;
    QLineEdit *lineEditAddress;
    QLabel *label_6;
    QLabel *label_61;
    QWidget *boxOptions;
    QVBoxLayout *layoutOptions;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *DataStreamCereal)
    {
        if (DataStreamCereal->objectName().isEmpty())
            DataStreamCereal->setObjectName(QString::fromUtf8("DataStreamCereal"));
        DataStreamCereal->resize(300, 150);
        verticalLayout = new QVBoxLayout(DataStreamCereal);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(DataStreamCereal);
        label->setObjectName(QString::fromUtf8("label"));
        QFont font;
        font.setPointSize(12);
        font.setBold(true);
        font.setWeight(75);
        label->setFont(font);

        verticalLayout->addWidget(label);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        lineEditAddress = new QLineEdit(DataStreamCereal);
        lineEditAddress->setObjectName(QString::fromUtf8("lineEditAddress"));

        gridLayout->addWidget(lineEditAddress, 0, 0, 1, 1);


        verticalLayout->addLayout(gridLayout);

        label_6 = new QLabel(DataStreamCereal);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        label_6->setFont(font);

        verticalLayout->addWidget(label_6);

        label_61 = new QLabel(DataStreamCereal);
        label_61->setObjectName(QString::fromUtf8("label_61"));
        label_61->setFont(font);

        verticalLayout->addWidget(label_61);

        boxOptions = new QWidget(DataStreamCereal);
        boxOptions->setObjectName(QString::fromUtf8("boxOptions"));
        layoutOptions = new QVBoxLayout(boxOptions);
        layoutOptions->setObjectName(QString::fromUtf8("layoutOptions"));
        layoutOptions->setContentsMargins(0, 6, 0, 6);

        verticalLayout->addWidget(boxOptions);

        buttonBox = new QDialogButtonBox(DataStreamCereal);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(DataStreamCereal);
        QObject::connect(buttonBox, SIGNAL(accepted()), DataStreamCereal, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), DataStreamCereal, SLOT(reject()));

        QMetaObject::connectSlotsByName(DataStreamCereal);
    } // setupUi

    void retranslateUi(QDialog *DataStreamCereal)
    {
        DataStreamCereal->setWindowTitle(QCoreApplication::translate("DataStreamCereal", "Cereal ZMQ Subscriber", nullptr));
        label->setText(QCoreApplication::translate("DataStreamCereal", "Connection (ZMQ_SUB)", nullptr));
        label_6->setText(QCoreApplication::translate("DataStreamCereal", "Using Backend: Cereal over ZMQ", nullptr));
        label_61->setText(QCoreApplication::translate("DataStreamCereal", "Make sure to run bridge (cereal/messaging/bridge)", nullptr));
    } // retranslateUi

};

namespace Ui {
    class DataStreamCereal: public Ui_DataStreamCereal {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_DATASTREAM_CEREAL_H
