/********************************************************************************
** Form generated from reading UI file 'colormap_editor.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_COLORMAP_EDITOR_H
#define UI_COLORMAP_EDITOR_H

#include <QCodeEditor>
#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_colormap_editor
{
public:
    QVBoxLayout *verticalLayout_3;
    QFrame *frame;
    QVBoxLayout *verticalLayout_4;
    QLabel *label_5;
    QHBoxLayout *horizontalLayout;
    QHBoxLayout *horizontalLayout_3;
    QVBoxLayout *verticalLayout_2;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label_2;
    QPushButton *buttonDelete;
    QSpacerItem *horizontalSpacer;
    QLabel *label_4;
    QListWidget *listWidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout_4;
    QLabel *label_3;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *buttonSave;
    QCodeEditor *functionText;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *colormap_editor)
    {
        if (colormap_editor->objectName().isEmpty())
            colormap_editor->setObjectName(QString::fromUtf8("colormap_editor"));
        colormap_editor->resize(851, 607);
        verticalLayout_3 = new QVBoxLayout(colormap_editor);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        frame = new QFrame(colormap_editor);
        frame->setObjectName(QString::fromUtf8("frame"));
        frame->setFrameShape(QFrame::Box);
        frame->setFrameShadow(QFrame::Plain);
        verticalLayout_4 = new QVBoxLayout(frame);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        label_5 = new QLabel(frame);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setWordWrap(true);
        label_5->setOpenExternalLinks(true);
        label_5->setTextInteractionFlags(Qt::TextBrowserInteraction);

        verticalLayout_4->addWidget(label_5);


        verticalLayout_3->addWidget(frame);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));

        verticalLayout_3->addLayout(horizontalLayout);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        label_2 = new QLabel(colormap_editor);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        label_2->setFont(font);

        horizontalLayout_2->addWidget(label_2);

        buttonDelete = new QPushButton(colormap_editor);
        buttonDelete->setObjectName(QString::fromUtf8("buttonDelete"));
        buttonDelete->setMinimumSize(QSize(26, 26));
        buttonDelete->setMaximumSize(QSize(26, 26));
        buttonDelete->setFlat(true);

        horizontalLayout_2->addWidget(buttonDelete);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer);


        verticalLayout_2->addLayout(horizontalLayout_2);

        label_4 = new QLabel(colormap_editor);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        verticalLayout_2->addWidget(label_4);

        listWidget = new QListWidget(colormap_editor);
        listWidget->setObjectName(QString::fromUtf8("listWidget"));

        verticalLayout_2->addWidget(listWidget);


        horizontalLayout_3->addLayout(verticalLayout_2);

        verticalLayout = new QVBoxLayout();
        verticalLayout->setSpacing(12);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalLayout_4->setContentsMargins(-1, 0, -1, 10);
        label_3 = new QLabel(colormap_editor);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setFont(font);

        horizontalLayout_4->addWidget(label_3);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_2);

        buttonSave = new QPushButton(colormap_editor);
        buttonSave->setObjectName(QString::fromUtf8("buttonSave"));

        horizontalLayout_4->addWidget(buttonSave);


        verticalLayout->addLayout(horizontalLayout_4);

        functionText = new QCodeEditor(colormap_editor);
        functionText->setObjectName(QString::fromUtf8("functionText"));

        verticalLayout->addWidget(functionText);


        horizontalLayout_3->addLayout(verticalLayout);

        horizontalLayout_3->setStretch(0, 2);
        horizontalLayout_3->setStretch(1, 3);

        verticalLayout_3->addLayout(horizontalLayout_3);

        buttonBox = new QDialogButtonBox(colormap_editor);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout_3->addWidget(buttonBox);


        retranslateUi(colormap_editor);
        QObject::connect(buttonBox, SIGNAL(accepted()), colormap_editor, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), colormap_editor, SLOT(reject()));

        QMetaObject::connectSlotsByName(colormap_editor);
    } // setupUi

    void retranslateUi(QDialog *colormap_editor)
    {
        colormap_editor->setWindowTitle(QCoreApplication::translate("colormap_editor", "ColorMap Editor", nullptr));
        label_5->setText(QCoreApplication::translate("colormap_editor", "<html><head/><body><p>For each value of the series, the <span style=\" font-weight:600;\">ColorMap</span> function must return a string containing a color. </p><p>You can use hexadecimal notation ('<span style=\" font-weight:600;\">#98fb98</span>') or any of <a href=\"https://doc.qt.io/qt-5/qml-color.html#svg-color-reference\"><span style=\" text-decoration: underline; color:#0000ff;\">these names</span></a> ('<span style=\" font-weight:600;\">palegreen</span>').</p><p>Return no parameters if you want to skip this point.</p></body></html>", nullptr));
        label_2->setText(QCoreApplication::translate("colormap_editor", "Saved ColorMaps:", nullptr));
        buttonDelete->setText(QString());
        label_4->setText(QCoreApplication::translate("colormap_editor", "Double-click to load.", nullptr));
        label_3->setText(QCoreApplication::translate("colormap_editor", "function ColorMap(v)", nullptr));
        buttonSave->setText(QCoreApplication::translate("colormap_editor", "Save", nullptr));
        functionText->setPlaceholderText(QCoreApplication::translate("colormap_editor", "--example: if v>0 then return \"red\" end", nullptr));
    } // retranslateUi

};

namespace Ui {
    class colormap_editor: public Ui_colormap_editor {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_COLORMAP_EDITOR_H
