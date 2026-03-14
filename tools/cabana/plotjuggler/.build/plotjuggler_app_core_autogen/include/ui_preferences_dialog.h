/********************************************************************************
** Form generated from reading UI file 'preferences_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PREFERENCES_DIALOG_H
#define UI_PREFERENCES_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PreferencesDialog
{
public:
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_8;
    QFormLayout *formLayout_3;
    QLabel *label;
    QComboBox *comboBoxTheme;
    QLabel *label_6;
    QFrame *frame_2;
    QVBoxLayout *verticalLayout_4;
    QCheckBox *checkBoxSeparator;
    QLabel *label_7;
    QFrame *frame_3;
    QVBoxLayout *verticalLayout_5;
    QCheckBox *checkBoxOpenGL;
    QSpacerItem *verticalSpacer_2;
    QWidget *behaviorTab;
    QVBoxLayout *verticalLayout_7;
    QGroupBox *groupBoxAutoZoom;
    QVBoxLayout *verticalLayout_6;
    QCheckBox *checkBoxAutoZoomAdded;
    QCheckBox *checkBoxAutoZoomVisibility;
    QCheckBox *checkBoxAutoZoomFilter;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout_2;
    QRadioButton *radioGlobalColorIndex;
    QRadioButton *radioLocalColorIndex;
    QCheckBox *checkBoxRememberColor;
    QSpacerItem *verticalSpacer;
    QWidget *tabPlugins;
    QVBoxLayout *verticalLayout_3;
    QHBoxLayout *horizontalLayout;
    QLabel *label_3;
    QSpacerItem *horizontalSpacer;
    QPushButton *pushButtonAdd;
    QPushButton *pushButtonRemove;
    QLabel *label_5;
    QListWidget *listWidgetCustom;
    QLabel *label_8;
    QListWidget *listWidgetBuiltin;
    QLabel *label_4;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *PreferencesDialog)
    {
        if (PreferencesDialog->objectName().isEmpty())
            PreferencesDialog->setObjectName(QString::fromUtf8("PreferencesDialog"));
        PreferencesDialog->resize(611, 506);
        PreferencesDialog->setMinimumSize(QSize(450, 0));
        verticalLayout = new QVBoxLayout(PreferencesDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        tabWidget = new QTabWidget(PreferencesDialog);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        tabWidget->setFocusPolicy(Qt::NoFocus);
        tab = new QWidget();
        tab->setObjectName(QString::fromUtf8("tab"));
        verticalLayout_8 = new QVBoxLayout(tab);
        verticalLayout_8->setObjectName(QString::fromUtf8("verticalLayout_8"));
        formLayout_3 = new QFormLayout();
        formLayout_3->setObjectName(QString::fromUtf8("formLayout_3"));
        formLayout_3->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        formLayout_3->setFormAlignment(Qt::AlignCenter);
        formLayout_3->setContentsMargins(-1, 10, -1, -1);
        label = new QLabel(tab);
        label->setObjectName(QString::fromUtf8("label"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy);

        formLayout_3->setWidget(0, QFormLayout::LabelRole, label);

        comboBoxTheme = new QComboBox(tab);
        comboBoxTheme->addItem(QString());
        comboBoxTheme->addItem(QString());
        comboBoxTheme->setObjectName(QString::fromUtf8("comboBoxTheme"));
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(comboBoxTheme->sizePolicy().hasHeightForWidth());
        comboBoxTheme->setSizePolicy(sizePolicy1);

        formLayout_3->setWidget(0, QFormLayout::FieldRole, comboBoxTheme);

        label_6 = new QLabel(tab);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        sizePolicy.setHeightForWidth(label_6->sizePolicy().hasHeightForWidth());
        label_6->setSizePolicy(sizePolicy);
        label_6->setMinimumSize(QSize(0, 40));

        formLayout_3->setWidget(1, QFormLayout::LabelRole, label_6);

        frame_2 = new QFrame(tab);
        frame_2->setObjectName(QString::fromUtf8("frame_2"));
        QSizePolicy sizePolicy2(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(frame_2->sizePolicy().hasHeightForWidth());
        frame_2->setSizePolicy(sizePolicy2);
        frame_2->setMinimumSize(QSize(0, 40));
        frame_2->setFrameShape(QFrame::StyledPanel);
        frame_2->setFrameShadow(QFrame::Raised);
        verticalLayout_4 = new QVBoxLayout(frame_2);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        checkBoxSeparator = new QCheckBox(frame_2);
        checkBoxSeparator->setObjectName(QString::fromUtf8("checkBoxSeparator"));
        checkBoxSeparator->setChecked(true);

        verticalLayout_4->addWidget(checkBoxSeparator);


        formLayout_3->setWidget(1, QFormLayout::FieldRole, frame_2);

        label_7 = new QLabel(tab);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        sizePolicy.setHeightForWidth(label_7->sizePolicy().hasHeightForWidth());
        label_7->setSizePolicy(sizePolicy);
        label_7->setMinimumSize(QSize(0, 40));

        formLayout_3->setWidget(2, QFormLayout::LabelRole, label_7);

        frame_3 = new QFrame(tab);
        frame_3->setObjectName(QString::fromUtf8("frame_3"));
        sizePolicy2.setHeightForWidth(frame_3->sizePolicy().hasHeightForWidth());
        frame_3->setSizePolicy(sizePolicy2);
        frame_3->setFrameShape(QFrame::StyledPanel);
        frame_3->setFrameShadow(QFrame::Raised);
        verticalLayout_5 = new QVBoxLayout(frame_3);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        checkBoxOpenGL = new QCheckBox(frame_3);
        checkBoxOpenGL->setObjectName(QString::fromUtf8("checkBoxOpenGL"));
        checkBoxOpenGL->setChecked(true);

        verticalLayout_5->addWidget(checkBoxOpenGL);


        formLayout_3->setWidget(2, QFormLayout::FieldRole, frame_3);


        verticalLayout_8->addLayout(formLayout_3);

        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_8->addItem(verticalSpacer_2);

        tabWidget->addTab(tab, QString());
        behaviorTab = new QWidget();
        behaviorTab->setObjectName(QString::fromUtf8("behaviorTab"));
        verticalLayout_7 = new QVBoxLayout(behaviorTab);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        groupBoxAutoZoom = new QGroupBox(behaviorTab);
        groupBoxAutoZoom->setObjectName(QString::fromUtf8("groupBoxAutoZoom"));
        QSizePolicy sizePolicy3(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(groupBoxAutoZoom->sizePolicy().hasHeightForWidth());
        groupBoxAutoZoom->setSizePolicy(sizePolicy3);
        groupBoxAutoZoom->setMinimumSize(QSize(0, 40));
        groupBoxAutoZoom->setAlignment(Qt::AlignLeading|Qt::AlignLeft|Qt::AlignVCenter);
        verticalLayout_6 = new QVBoxLayout(groupBoxAutoZoom);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        checkBoxAutoZoomAdded = new QCheckBox(groupBoxAutoZoom);
        checkBoxAutoZoomAdded->setObjectName(QString::fromUtf8("checkBoxAutoZoomAdded"));
        checkBoxAutoZoomAdded->setChecked(true);

        verticalLayout_6->addWidget(checkBoxAutoZoomAdded);

        checkBoxAutoZoomVisibility = new QCheckBox(groupBoxAutoZoom);
        checkBoxAutoZoomVisibility->setObjectName(QString::fromUtf8("checkBoxAutoZoomVisibility"));
        checkBoxAutoZoomVisibility->setChecked(true);

        verticalLayout_6->addWidget(checkBoxAutoZoomVisibility);

        checkBoxAutoZoomFilter = new QCheckBox(groupBoxAutoZoom);
        checkBoxAutoZoomFilter->setObjectName(QString::fromUtf8("checkBoxAutoZoomFilter"));
        checkBoxAutoZoomFilter->setChecked(true);

        verticalLayout_6->addWidget(checkBoxAutoZoomFilter);


        verticalLayout_7->addWidget(groupBoxAutoZoom);

        groupBox = new QGroupBox(behaviorTab);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        sizePolicy2.setHeightForWidth(groupBox->sizePolicy().hasHeightForWidth());
        groupBox->setSizePolicy(sizePolicy2);
        verticalLayout_2 = new QVBoxLayout(groupBox);
        verticalLayout_2->setSpacing(0);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        radioGlobalColorIndex = new QRadioButton(groupBox);
        radioGlobalColorIndex->setObjectName(QString::fromUtf8("radioGlobalColorIndex"));
        radioGlobalColorIndex->setChecked(true);

        verticalLayout_2->addWidget(radioGlobalColorIndex);

        radioLocalColorIndex = new QRadioButton(groupBox);
        radioLocalColorIndex->setObjectName(QString::fromUtf8("radioLocalColorIndex"));

        verticalLayout_2->addWidget(radioLocalColorIndex);

        checkBoxRememberColor = new QCheckBox(groupBox);
        checkBoxRememberColor->setObjectName(QString::fromUtf8("checkBoxRememberColor"));
        checkBoxRememberColor->setChecked(true);

        verticalLayout_2->addWidget(checkBoxRememberColor);


        verticalLayout_7->addWidget(groupBox);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_7->addItem(verticalSpacer);

        tabWidget->addTab(behaviorTab, QString());
        tabPlugins = new QWidget();
        tabPlugins->setObjectName(QString::fromUtf8("tabPlugins"));
        verticalLayout_3 = new QVBoxLayout(tabPlugins);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label_3 = new QLabel(tabPlugins);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        horizontalLayout->addWidget(label_3);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        pushButtonAdd = new QPushButton(tabPlugins);
        pushButtonAdd->setObjectName(QString::fromUtf8("pushButtonAdd"));
        pushButtonAdd->setMinimumSize(QSize(24, 24));
        pushButtonAdd->setMaximumSize(QSize(24, 24));
        pushButtonAdd->setIconSize(QSize(22, 22));
        pushButtonAdd->setFlat(true);

        horizontalLayout->addWidget(pushButtonAdd);

        pushButtonRemove = new QPushButton(tabPlugins);
        pushButtonRemove->setObjectName(QString::fromUtf8("pushButtonRemove"));
        pushButtonRemove->setMinimumSize(QSize(24, 24));
        pushButtonRemove->setMaximumSize(QSize(24, 24));
        pushButtonRemove->setIconSize(QSize(20, 20));
        pushButtonRemove->setFlat(true);

        horizontalLayout->addWidget(pushButtonRemove);


        verticalLayout_3->addLayout(horizontalLayout);

        label_5 = new QLabel(tabPlugins);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setWordWrap(true);

        verticalLayout_3->addWidget(label_5);

        listWidgetCustom = new QListWidget(tabPlugins);
        listWidgetCustom->setObjectName(QString::fromUtf8("listWidgetCustom"));
        listWidgetCustom->setFocusPolicy(Qt::ClickFocus);
        listWidgetCustom->setEditTriggers(QAbstractItemView::NoEditTriggers);
        listWidgetCustom->setDragDropMode(QAbstractItemView::InternalMove);
        listWidgetCustom->setDefaultDropAction(Qt::MoveAction);
        listWidgetCustom->setSpacing(2);

        verticalLayout_3->addWidget(listWidgetCustom);

        label_8 = new QLabel(tabPlugins);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        verticalLayout_3->addWidget(label_8);

        listWidgetBuiltin = new QListWidget(tabPlugins);
        listWidgetBuiltin->setObjectName(QString::fromUtf8("listWidgetBuiltin"));
        listWidgetBuiltin->setEditTriggers(QAbstractItemView::NoEditTriggers);
        listWidgetBuiltin->setSelectionMode(QAbstractItemView::NoSelection);
        listWidgetBuiltin->setSpacing(2);

        verticalLayout_3->addWidget(listWidgetBuiltin);

        label_4 = new QLabel(tabPlugins);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setWordWrap(true);

        verticalLayout_3->addWidget(label_4);

        tabWidget->addTab(tabPlugins, QString());

        verticalLayout->addWidget(tabWidget);

        buttonBox = new QDialogButtonBox(PreferencesDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(PreferencesDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), PreferencesDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), PreferencesDialog, SLOT(reject()));

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(PreferencesDialog);
    } // setupUi

    void retranslateUi(QDialog *PreferencesDialog)
    {
        PreferencesDialog->setWindowTitle(QCoreApplication::translate("PreferencesDialog", "Preferences", nullptr));
        label->setText(QCoreApplication::translate("PreferencesDialog", "Theme:", nullptr));
        comboBoxTheme->setItemText(0, QCoreApplication::translate("PreferencesDialog", "Light Theme", nullptr));
        comboBoxTheme->setItemText(1, QCoreApplication::translate("PreferencesDialog", "Dark Theme", nullptr));

#if QT_CONFIG(tooltip)
        label_6->setToolTip(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p>The &quot;Name Separator&quot; is the character use to split the name of a timeseries in the tree view (Timeseries Panel).</p><p>Default is <span style=\" font-weight:600;\">&quot;/&quot;</span>.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        label_6->setText(QCoreApplication::translate("PreferencesDialog", "Tree view:", nullptr));
#if QT_CONFIG(tooltip)
        checkBoxSeparator->setToolTip(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p>Change will not be applied to existing timeseries.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        checkBoxSeparator->setText(QCoreApplication::translate("PreferencesDialog", "enabled (using separator \"/\" in the name)", nullptr));
        label_7->setText(QCoreApplication::translate("PreferencesDialog", "OpenGL:", nullptr));
#if QT_CONFIG(tooltip)
        checkBoxOpenGL->setToolTip(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p>Change will not be applied to existing plots.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        checkBoxOpenGL->setText(QCoreApplication::translate("PreferencesDialog", "enabled", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab), QCoreApplication::translate("PreferencesDialog", "Appearance", nullptr));
        groupBoxAutoZoom->setTitle(QCoreApplication::translate("PreferencesDialog", "Reset plot zoom when:", nullptr));
        checkBoxAutoZoomAdded->setText(QCoreApplication::translate("PreferencesDialog", "Curve added to plot", nullptr));
        checkBoxAutoZoomVisibility->setText(QCoreApplication::translate("PreferencesDialog", "Curve visibility toggled (in curves legend)", nullptr));
#if QT_CONFIG(tooltip)
        checkBoxAutoZoomFilter->setToolTip(QString());
#endif // QT_CONFIG(tooltip)
        checkBoxAutoZoomFilter->setText(QCoreApplication::translate("PreferencesDialog", "Filters applied to curves", nullptr));
        groupBox->setTitle(QCoreApplication::translate("PreferencesDialog", "Curves color:", nullptr));
        radioGlobalColorIndex->setText(QCoreApplication::translate("PreferencesDialog", "global color sequence", nullptr));
        radioLocalColorIndex->setText(QCoreApplication::translate("PreferencesDialog", "reset color sequence in each plot area", nullptr));
        checkBoxRememberColor->setText(QCoreApplication::translate("PreferencesDialog", "remember color", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(behaviorTab), QCoreApplication::translate("PreferencesDialog", "Behavior", nullptr));
        label_3->setText(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p><span style=\" font-weight:600;\">Plugin folders </span>(will be loaded in this order)<span style=\" font-weight:600;\">:</span></p></body></html>", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonAdd->setToolTip(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p>Add folder</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonAdd->setText(QString());
#if QT_CONFIG(tooltip)
        pushButtonRemove->setToolTip(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p>Remove selected</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonRemove->setText(QString());
        label_5->setText(QCoreApplication::translate("PreferencesDialog", "Add custom folders. Drag and drop the items to change the order.", nullptr));
        label_8->setText(QCoreApplication::translate("PreferencesDialog", "List of built-in folders:", nullptr));
        label_4->setText(QCoreApplication::translate("PreferencesDialog", "<html><head/><body><p><span style=\" font-weight:600;\">Note</span>: this change will take effect the next time PlotJuggler is started</p></body></html>", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tabPlugins), QCoreApplication::translate("PreferencesDialog", "Plugins", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PreferencesDialog: public Ui_PreferencesDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PREFERENCES_DIALOG_H
