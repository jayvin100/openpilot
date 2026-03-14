/********************************************************************************
** Form generated from reading UI file 'first_derivative.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_FIRST_DERIVATIVE_H
#define UI_FIRST_DERIVATIVE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_FirstDerivariveForm
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QFrame *frame;
    QFormLayout *formLayout;
    QRadioButton *radioActual;
    QLabel *label_2;
    QRadioButton *radioCustom;
    QLineEdit *lineEditCustom;
    QPushButton *buttonCompute;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *FirstDerivariveForm)
    {
        if (FirstDerivariveForm->objectName().isEmpty())
            FirstDerivariveForm->setObjectName(QString::fromUtf8("FirstDerivariveForm"));
        FirstDerivariveForm->resize(361, 240);
        FirstDerivariveForm->setMinimumSize(QSize(200, 0));
        verticalLayout = new QVBoxLayout(FirstDerivariveForm);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(FirstDerivariveForm);
        label->setObjectName(QString::fromUtf8("label"));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        label->setFont(font);

        verticalLayout->addWidget(label);

        frame = new QFrame(FirstDerivariveForm);
        frame->setObjectName(QString::fromUtf8("frame"));
        formLayout = new QFormLayout(frame);
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        radioActual = new QRadioButton(frame);
        radioActual->setObjectName(QString::fromUtf8("radioActual"));
        radioActual->setChecked(true);

        formLayout->setWidget(1, QFormLayout::LabelRole, radioActual);

        label_2 = new QLabel(frame);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::FieldRole, label_2);

        radioCustom = new QRadioButton(frame);
        radioCustom->setObjectName(QString::fromUtf8("radioCustom"));

        formLayout->setWidget(2, QFormLayout::LabelRole, radioCustom);

        lineEditCustom = new QLineEdit(frame);
        lineEditCustom->setObjectName(QString::fromUtf8("lineEditCustom"));
        lineEditCustom->setMaximumSize(QSize(80, 16777215));

        formLayout->setWidget(2, QFormLayout::FieldRole, lineEditCustom);

        buttonCompute = new QPushButton(frame);
        buttonCompute->setObjectName(QString::fromUtf8("buttonCompute"));
        buttonCompute->setMaximumSize(QSize(80, 16777215));

        formLayout->setWidget(3, QFormLayout::FieldRole, buttonCompute);


        verticalLayout->addWidget(frame);

        verticalSpacer = new QSpacerItem(20, 57, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(FirstDerivariveForm);

        QMetaObject::connectSlotsByName(FirstDerivariveForm);
    } // setupUi

    void retranslateUi(QWidget *FirstDerivariveForm)
    {
        FirstDerivariveForm->setWindowTitle(QCoreApplication::translate("FirstDerivariveForm", "Form", nullptr));
        label->setText(QCoreApplication::translate("FirstDerivariveForm", "Time Difference (dT):", nullptr));
#if QT_CONFIG(tooltip)
        radioActual->setToolTip(QCoreApplication::translate("FirstDerivariveForm", "<html><head/><body><p>use the actual difference in time between two consecutive points.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        radioActual->setText(QCoreApplication::translate("FirstDerivariveForm", "Actual ", nullptr));
        label_2->setText(QCoreApplication::translate("FirstDerivariveForm", "(It might be noisy!)", nullptr));
#if QT_CONFIG(tooltip)
        radioCustom->setToolTip(QCoreApplication::translate("FirstDerivariveForm", "<html><head/><body><p>Use a fixed, custom value of dT.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        radioCustom->setText(QCoreApplication::translate("FirstDerivariveForm", "Custom:", nullptr));
        lineEditCustom->setText(QCoreApplication::translate("FirstDerivariveForm", "1.0", nullptr));
#if QT_CONFIG(tooltip)
        buttonCompute->setToolTip(QCoreApplication::translate("FirstDerivariveForm", "<html><head/><body><p>Calculate the corret delta time.</p><p>It tries to filter out outliers.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonCompute->setText(QCoreApplication::translate("FirstDerivariveForm", "Guess dT", nullptr));
    } // retranslateUi

};

namespace Ui {
    class FirstDerivariveForm: public Ui_FirstDerivariveForm {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_FIRST_DERIVATIVE_H
