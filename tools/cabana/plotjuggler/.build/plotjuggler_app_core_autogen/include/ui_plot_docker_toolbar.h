/********************************************************************************
** Form generated from reading UI file 'plot_docker_toolbar.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PLOT_DOCKER_TOOLBAR_H
#define UI_PLOT_DOCKER_TOOLBAR_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_DraggableToolbar
{
public:
    QHBoxLayout *mainHorizontalLayout;
    QSpacerItem *horizontalSpacer;
    QLabel *label;
    QWidget *widgetButtons;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *buttonBackground;
    QPushButton *buttonSplitHorizontal;
    QPushButton *buttonSplitVertical;
    QPushButton *buttonFullscreen;
    QPushButton *buttonClose;

    void setupUi(QWidget *DraggableToolbar)
    {
        if (DraggableToolbar->objectName().isEmpty())
            DraggableToolbar->setObjectName(QString::fromUtf8("DraggableToolbar"));
        DraggableToolbar->resize(712, 30);
        DraggableToolbar->setMinimumSize(QSize(0, 28));
        DraggableToolbar->setMaximumSize(QSize(16777215, 30));
        mainHorizontalLayout = new QHBoxLayout(DraggableToolbar);
        mainHorizontalLayout->setSpacing(0);
        mainHorizontalLayout->setObjectName(QString::fromUtf8("mainHorizontalLayout"));
        mainHorizontalLayout->setContentsMargins(2, 2, 2, 2);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        mainHorizontalLayout->addItem(horizontalSpacer);

        label = new QLabel(DraggableToolbar);
        label->setObjectName(QString::fromUtf8("label"));
        label->setAlignment(Qt::AlignCenter);

        mainHorizontalLayout->addWidget(label);

        widgetButtons = new QWidget(DraggableToolbar);
        widgetButtons->setObjectName(QString::fromUtf8("widgetButtons"));
        widgetButtons->setMinimumSize(QSize(110, 24));
        widgetButtons->setMaximumSize(QSize(110, 24));
        horizontalLayout = new QHBoxLayout(widgetButtons);
        horizontalLayout->setSpacing(0);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalSpacer_2 = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_2);

        buttonBackground = new QPushButton(widgetButtons);
        buttonBackground->setObjectName(QString::fromUtf8("buttonBackground"));
        buttonBackground->setMinimumSize(QSize(24, 24));
        buttonBackground->setMaximumSize(QSize(24, 24));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/resources/svg/color_background.svg"), QSize(), QIcon::Normal, QIcon::Off);
        buttonBackground->setIcon(icon);
        buttonBackground->setCheckable(false);
        buttonBackground->setFlat(true);

        horizontalLayout->addWidget(buttonBackground);

        buttonSplitHorizontal = new QPushButton(widgetButtons);
        buttonSplitHorizontal->setObjectName(QString::fromUtf8("buttonSplitHorizontal"));
        buttonSplitHorizontal->setMinimumSize(QSize(24, 24));
        buttonSplitHorizontal->setMaximumSize(QSize(24, 24));
        buttonSplitHorizontal->setFocusPolicy(Qt::NoFocus);
        QIcon icon1;
        icon1.addFile(QString::fromUtf8(":/resources/svg/add_column.svg"), QSize(), QIcon::Normal, QIcon::Off);
        buttonSplitHorizontal->setIcon(icon1);
        buttonSplitHorizontal->setIconSize(QSize(20, 20));
        buttonSplitHorizontal->setFlat(true);

        horizontalLayout->addWidget(buttonSplitHorizontal);

        buttonSplitVertical = new QPushButton(widgetButtons);
        buttonSplitVertical->setObjectName(QString::fromUtf8("buttonSplitVertical"));
        buttonSplitVertical->setMinimumSize(QSize(24, 24));
        buttonSplitVertical->setMaximumSize(QSize(24, 24));
        buttonSplitVertical->setFocusPolicy(Qt::NoFocus);
        QIcon icon2;
        icon2.addFile(QString::fromUtf8(":/resources/svg/add_row.svg"), QSize(), QIcon::Normal, QIcon::Off);
        buttonSplitVertical->setIcon(icon2);
        buttonSplitVertical->setIconSize(QSize(20, 20));
        buttonSplitVertical->setFlat(true);

        horizontalLayout->addWidget(buttonSplitVertical);

        buttonFullscreen = new QPushButton(widgetButtons);
        buttonFullscreen->setObjectName(QString::fromUtf8("buttonFullscreen"));
        buttonFullscreen->setMinimumSize(QSize(24, 24));
        buttonFullscreen->setMaximumSize(QSize(24, 24));
        buttonFullscreen->setFocusPolicy(Qt::NoFocus);
        QIcon icon3;
        icon3.addFile(QString::fromUtf8(":/resources/svg/expand.svg"), QSize(), QIcon::Normal, QIcon::Off);
        buttonFullscreen->setIcon(icon3);
        buttonFullscreen->setIconSize(QSize(20, 20));
        buttonFullscreen->setCheckable(false);
        buttonFullscreen->setFlat(true);

        horizontalLayout->addWidget(buttonFullscreen);


        mainHorizontalLayout->addWidget(widgetButtons);

        buttonClose = new QPushButton(DraggableToolbar);
        buttonClose->setObjectName(QString::fromUtf8("buttonClose"));
        buttonClose->setMinimumSize(QSize(24, 24));
        buttonClose->setMaximumSize(QSize(24, 24));
        buttonClose->setFocusPolicy(Qt::NoFocus);
        QIcon icon4;
        icon4.addFile(QString::fromUtf8(":/resources/svg/close-button.svg"), QSize(), QIcon::Normal, QIcon::Off);
        buttonClose->setIcon(icon4);
        buttonClose->setIconSize(QSize(20, 20));
        buttonClose->setFlat(true);

        mainHorizontalLayout->addWidget(buttonClose);

        mainHorizontalLayout->setStretch(1, 1);

        retranslateUi(DraggableToolbar);

        QMetaObject::connectSlotsByName(DraggableToolbar);
    } // setupUi

    void retranslateUi(QWidget *DraggableToolbar)
    {
        DraggableToolbar->setWindowTitle(QCoreApplication::translate("DraggableToolbar", "Form", nullptr));
        label->setText(QCoreApplication::translate("DraggableToolbar", "TextLabel", nullptr));
#if QT_CONFIG(tooltip)
        buttonBackground->setToolTip(QCoreApplication::translate("DraggableToolbar", "<html><head/><body><p><span style=\" font-weight:600;\">Drag and Drop </span>a timeseries here, to assign a color to the background, using the ColorMap.</p><p>Click this button to change it.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonBackground->setText(QString());
        buttonSplitHorizontal->setText(QString());
        buttonSplitVertical->setText(QString());
#if QT_CONFIG(tooltip)
        buttonFullscreen->setToolTip(QCoreApplication::translate("DraggableToolbar", "<html><head/><body><p>Fullscreen</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonFullscreen->setText(QString());
#if QT_CONFIG(tooltip)
        buttonClose->setToolTip(QCoreApplication::translate("DraggableToolbar", "<html><head/><body><p>Close Plot Area</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonClose->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class DraggableToolbar: public Ui_DraggableToolbar {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PLOT_DOCKER_TOOLBAR_H
