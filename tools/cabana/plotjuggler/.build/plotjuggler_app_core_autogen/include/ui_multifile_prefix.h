/********************************************************************************
** Form generated from reading UI file 'multifile_prefix.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MULTIFILE_PREFIX_H
#define UI_MULTIFILE_PREFIX_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_DialogMultifilePrefix
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QFrame *frame;
    QVBoxLayout *verticalLayoutFrame;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *DialogMultifilePrefix)
    {
        if (DialogMultifilePrefix->objectName().isEmpty())
            DialogMultifilePrefix->setObjectName(QString::fromUtf8("DialogMultifilePrefix"));
        DialogMultifilePrefix->resize(600, 303);
        DialogMultifilePrefix->setMinimumSize(QSize(600, 0));
        verticalLayout = new QVBoxLayout(DialogMultifilePrefix);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(DialogMultifilePrefix);
        label->setObjectName(QString::fromUtf8("label"));
        label->setWordWrap(true);

        verticalLayout->addWidget(label);

        frame = new QFrame(DialogMultifilePrefix);
        frame->setObjectName(QString::fromUtf8("frame"));
        frame->setFrameShape(QFrame::Box);
        frame->setFrameShadow(QFrame::Plain);
        verticalLayoutFrame = new QVBoxLayout(frame);
        verticalLayoutFrame->setObjectName(QString::fromUtf8("verticalLayoutFrame"));
        verticalSpacer = new QSpacerItem(20, 191, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayoutFrame->addItem(verticalSpacer);


        verticalLayout->addWidget(frame);

        buttonBox = new QDialogButtonBox(DialogMultifilePrefix);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(DialogMultifilePrefix);
        QObject::connect(buttonBox, SIGNAL(accepted()), DialogMultifilePrefix, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), DialogMultifilePrefix, SLOT(reject()));

        QMetaObject::connectSlotsByName(DialogMultifilePrefix);
    } // setupUi

    void retranslateUi(QDialog *DialogMultifilePrefix)
    {
        DialogMultifilePrefix->setWindowTitle(QCoreApplication::translate("DialogMultifilePrefix", "Dialog", nullptr));
        label->setText(QCoreApplication::translate("DialogMultifilePrefix", "<html><head/><body><p>Multiple files are being loaded. A <span style=\" font-weight:600;\">prefix</span> will be added.</p></body></html>", nullptr));
    } // retranslateUi

};

namespace Ui {
    class DialogMultifilePrefix: public Ui_DialogMultifilePrefix {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MULTIFILE_PREFIX_H
