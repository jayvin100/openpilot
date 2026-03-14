/********************************************************************************
** Form generated from reading UI file 'function_editor.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_FUNCTION_EDITOR_H
#define UI_FUNCTION_EDITOR_H

#include <QCodeEditor>
#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_FunctionEditor
{
public:
    QVBoxLayout *verticalLayout_3;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_5;
    QFrame *framePlotPreview;
    QHBoxLayout *horizontalLayout_5;
    QWidget *leftWidget;
    QVBoxLayout *leftLayout;
    QLabel *label_linkeChannel;
    QLineEdit *lineEditSource;
    QHBoxLayout *horizontalLayout_4;
    QLabel *label_2;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *pushButtonDeleteCurves;
    QTableWidget *listAdditionalSources;
    QWidget *widget;
    QVBoxLayout *verticalLayout_2;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label_5;
    QPushButton *buttonLoadFunctions;
    QPushButton *buttonSaveFunctions;
    QPushButton *buttonSaveCurrent;
    QSpacerItem *spacer;
    QListWidget *snippetsListSaved;
    QLabel *label_7;
    QPlainTextEdit *snippetPreview;
    QWidget *rightWidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout_3;
    QLabel *label_name;
    QLineEdit *nameLineEdit;
    QLabel *labelSemaphore;
    QHBoxLayout *horizontalLayout_6;
    QLabel *label;
    QSpacerItem *horizontalSpacer_3;
    QPushButton *pushButtonHelp;
    QWidget *widget_2;
    QVBoxLayout *verticalLayout_4;
    QCodeEditor *globalVarsText;
    QLabel *labelFunction;
    QCodeEditor *functionText;
    QWidget *tab_2;
    QVBoxLayout *verticalLayout_9;
    QSplitter *splitter;
    QWidget *layoutWidget;
    QVBoxLayout *verticalLayout_6;
    QLabel *label_6;
    QHBoxLayout *horizontalLayout_7;
    QLabel *label_3;
    QLineEdit *lineEditTab2Filter;
    QHBoxLayout *horizontalLayout_10;
    QRadioButton *radioButtonContains;
    QRadioButton *radioButtonWildcard;
    QRadioButton *radioButtonRegExp;
    QSpacerItem *horizontalSpacer_5;
    QListWidget *listBatchSources;
    QWidget *rightWidget_2;
    QVBoxLayout *verticalLayout_7;
    QLabel *label_8;
    QFrame *line;
    QHBoxLayout *horizontalLayout_8;
    QRadioButton *radioButtonPrefix;
    QRadioButton *radioButtonSuffix;
    QLineEdit *suffixLineEdit;
    QLabel *labelSemaphoreBatch;
    QPushButton *pushButtonHelpTab2;
    QHBoxLayout *horizontalLayout_9;
    QLabel *label_4;
    QSpacerItem *horizontalSpacer_4;
    QWidget *widget_3;
    QVBoxLayout *verticalLayout_8;
    QCodeEditor *globalVarsTextBatch;
    QLabel *labelFunction_2;
    QCodeEditor *functionTextBatch;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer;
    QPushButton *pushButtonCreate;
    QPushButton *pushButtonCancel;

    void setupUi(QWidget *FunctionEditor)
    {
        if (FunctionEditor->objectName().isEmpty())
            FunctionEditor->setObjectName(QString::fromUtf8("FunctionEditor"));
        FunctionEditor->resize(1228, 902);
        FunctionEditor->setMinimumSize(QSize(620, 600));
        FunctionEditor->setMaximumSize(QSize(16777215, 16777215));
        verticalLayout_3 = new QVBoxLayout(FunctionEditor);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        tabWidget = new QTabWidget(FunctionEditor);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        tab = new QWidget();
        tab->setObjectName(QString::fromUtf8("tab"));
        verticalLayout_5 = new QVBoxLayout(tab);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        framePlotPreview = new QFrame(tab);
        framePlotPreview->setObjectName(QString::fromUtf8("framePlotPreview"));
        framePlotPreview->setMinimumSize(QSize(600, 200));
        framePlotPreview->setFrameShape(QFrame::NoFrame);

        verticalLayout_5->addWidget(framePlotPreview);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setSpacing(10);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalLayout_5->setContentsMargins(-1, 0, -1, -1);
        leftWidget = new QWidget(tab);
        leftWidget->setObjectName(QString::fromUtf8("leftWidget"));
        leftWidget->setMinimumSize(QSize(295, 0));
        leftLayout = new QVBoxLayout(leftWidget);
        leftLayout->setObjectName(QString::fromUtf8("leftLayout"));
        leftLayout->setContentsMargins(0, -1, 8, -1);
        label_linkeChannel = new QLabel(leftWidget);
        label_linkeChannel->setObjectName(QString::fromUtf8("label_linkeChannel"));
        label_linkeChannel->setMinimumSize(QSize(0, 25));
        label_linkeChannel->setWordWrap(true);

        leftLayout->addWidget(label_linkeChannel);

        lineEditSource = new QLineEdit(leftWidget);
        lineEditSource->setObjectName(QString::fromUtf8("lineEditSource"));
        lineEditSource->setReadOnly(true);

        leftLayout->addWidget(lineEditSource);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalLayout_4->setContentsMargins(0, 0, -1, -1);
        label_2 = new QLabel(leftWidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        horizontalLayout_4->addWidget(label_2);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_2);

        pushButtonDeleteCurves = new QPushButton(leftWidget);
        pushButtonDeleteCurves->setObjectName(QString::fromUtf8("pushButtonDeleteCurves"));
        pushButtonDeleteCurves->setEnabled(false);
        pushButtonDeleteCurves->setMinimumSize(QSize(24, 24));
        pushButtonDeleteCurves->setMaximumSize(QSize(24, 24));
        pushButtonDeleteCurves->setFocusPolicy(Qt::NoFocus);
        pushButtonDeleteCurves->setIconSize(QSize(22, 22));
        pushButtonDeleteCurves->setFlat(true);

        horizontalLayout_4->addWidget(pushButtonDeleteCurves);


        leftLayout->addLayout(horizontalLayout_4);

        listAdditionalSources = new QTableWidget(leftWidget);
        if (listAdditionalSources->columnCount() < 2)
            listAdditionalSources->setColumnCount(2);
        listAdditionalSources->setObjectName(QString::fromUtf8("listAdditionalSources"));
        listAdditionalSources->setEditTriggers(QAbstractItemView::NoEditTriggers);
        listAdditionalSources->setDragDropMode(QAbstractItemView::DropOnly);
        listAdditionalSources->setSelectionMode(QAbstractItemView::ExtendedSelection);
        listAdditionalSources->setSelectionBehavior(QAbstractItemView::SelectRows);
        listAdditionalSources->setColumnCount(2);
        listAdditionalSources->horizontalHeader()->setVisible(false);
        listAdditionalSources->horizontalHeader()->setMinimumSectionSize(40);
        listAdditionalSources->horizontalHeader()->setDefaultSectionSize(50);
        listAdditionalSources->horizontalHeader()->setStretchLastSection(true);
        listAdditionalSources->verticalHeader()->setVisible(false);

        leftLayout->addWidget(listAdditionalSources);

        widget = new QWidget(leftWidget);
        widget->setObjectName(QString::fromUtf8("widget"));
        widget->setMinimumSize(QSize(0, 100));
        verticalLayout_2 = new QVBoxLayout(widget);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(0, 0, -1, -1);
        label_5 = new QLabel(widget);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        horizontalLayout_2->addWidget(label_5);

        buttonLoadFunctions = new QPushButton(widget);
        buttonLoadFunctions->setObjectName(QString::fromUtf8("buttonLoadFunctions"));
        buttonLoadFunctions->setMinimumSize(QSize(30, 30));
        buttonLoadFunctions->setMaximumSize(QSize(30, 30));
        buttonLoadFunctions->setFocusPolicy(Qt::NoFocus);
        buttonLoadFunctions->setIconSize(QSize(26, 26));
        buttonLoadFunctions->setFlat(true);

        horizontalLayout_2->addWidget(buttonLoadFunctions);

        buttonSaveFunctions = new QPushButton(widget);
        buttonSaveFunctions->setObjectName(QString::fromUtf8("buttonSaveFunctions"));
        buttonSaveFunctions->setEnabled(true);
        buttonSaveFunctions->setMinimumSize(QSize(30, 30));
        buttonSaveFunctions->setMaximumSize(QSize(30, 30));
        buttonSaveFunctions->setFocusPolicy(Qt::NoFocus);
        buttonSaveFunctions->setIconSize(QSize(26, 26));
        buttonSaveFunctions->setFlat(true);

        horizontalLayout_2->addWidget(buttonSaveFunctions);

        buttonSaveCurrent = new QPushButton(widget);
        buttonSaveCurrent->setObjectName(QString::fromUtf8("buttonSaveCurrent"));
        buttonSaveCurrent->setEnabled(true);
        buttonSaveCurrent->setMinimumSize(QSize(30, 30));
        buttonSaveCurrent->setMaximumSize(QSize(30, 30));
        buttonSaveCurrent->setFocusPolicy(Qt::NoFocus);
        buttonSaveCurrent->setIconSize(QSize(26, 26));
        buttonSaveCurrent->setFlat(true);

        horizontalLayout_2->addWidget(buttonSaveCurrent);

        spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(spacer);


        verticalLayout_2->addLayout(horizontalLayout_2);

        snippetsListSaved = new QListWidget(widget);
        snippetsListSaved->setObjectName(QString::fromUtf8("snippetsListSaved"));

        verticalLayout_2->addWidget(snippetsListSaved);

        label_7 = new QLabel(widget);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        label_7->setFont(font);
        label_7->setAlignment(Qt::AlignCenter);

        verticalLayout_2->addWidget(label_7);

        snippetPreview = new QPlainTextEdit(widget);
        snippetPreview->setObjectName(QString::fromUtf8("snippetPreview"));
        snippetPreview->setFocusPolicy(Qt::NoFocus);
        snippetPreview->setReadOnly(true);

        verticalLayout_2->addWidget(snippetPreview);

        verticalLayout_2->setStretch(1, 2);
        verticalLayout_2->setStretch(3, 3);

        leftLayout->addWidget(widget);


        horizontalLayout_5->addWidget(leftWidget);

        rightWidget = new QWidget(tab);
        rightWidget->setObjectName(QString::fromUtf8("rightWidget"));
        rightWidget->setMinimumSize(QSize(295, 0));
        verticalLayout = new QVBoxLayout(rightWidget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        label_name = new QLabel(rightWidget);
        label_name->setObjectName(QString::fromUtf8("label_name"));
        label_name->setMinimumSize(QSize(0, 25));

        horizontalLayout_3->addWidget(label_name);

        nameLineEdit = new QLineEdit(rightWidget);
        nameLineEdit->setObjectName(QString::fromUtf8("nameLineEdit"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(nameLineEdit->sizePolicy().hasHeightForWidth());
        nameLineEdit->setSizePolicy(sizePolicy);
        nameLineEdit->setMinimumSize(QSize(0, 25));
        nameLineEdit->setFocusPolicy(Qt::ClickFocus);

        horizontalLayout_3->addWidget(nameLineEdit);

        labelSemaphore = new QLabel(rightWidget);
        labelSemaphore->setObjectName(QString::fromUtf8("labelSemaphore"));
        labelSemaphore->setMinimumSize(QSize(30, 30));
        labelSemaphore->setMaximumSize(QSize(30, 30));

        horizontalLayout_3->addWidget(labelSemaphore);

        horizontalLayout_3->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout_3);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName(QString::fromUtf8("horizontalLayout_6"));
        horizontalLayout_6->setContentsMargins(-1, 11, -1, -1);
        label = new QLabel(rightWidget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setWordWrap(true);
        label->setOpenExternalLinks(true);

        horizontalLayout_6->addWidget(label);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_3);

        pushButtonHelp = new QPushButton(rightWidget);
        pushButtonHelp->setObjectName(QString::fromUtf8("pushButtonHelp"));
        pushButtonHelp->setMaximumSize(QSize(50, 16777215));

        horizontalLayout_6->addWidget(pushButtonHelp);


        verticalLayout->addLayout(horizontalLayout_6);

        widget_2 = new QWidget(rightWidget);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        verticalLayout_4 = new QVBoxLayout(widget_2);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        verticalLayout_4->setContentsMargins(0, 0, 0, 0);
        globalVarsText = new QCodeEditor(widget_2);
        globalVarsText->setObjectName(QString::fromUtf8("globalVarsText"));
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(globalVarsText->sizePolicy().hasHeightForWidth());
        globalVarsText->setSizePolicy(sizePolicy1);
        globalVarsText->setMinimumSize(QSize(0, 0));

        verticalLayout_4->addWidget(globalVarsText);

        labelFunction = new QLabel(widget_2);
        labelFunction->setObjectName(QString::fromUtf8("labelFunction"));

        verticalLayout_4->addWidget(labelFunction);

        functionText = new QCodeEditor(widget_2);
        functionText->setObjectName(QString::fromUtf8("functionText"));
        functionText->setMinimumSize(QSize(0, 0));

        verticalLayout_4->addWidget(functionText);


        verticalLayout->addWidget(widget_2);


        horizontalLayout_5->addWidget(rightWidget);


        verticalLayout_5->addLayout(horizontalLayout_5);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName(QString::fromUtf8("tab_2"));
        verticalLayout_9 = new QVBoxLayout(tab_2);
        verticalLayout_9->setObjectName(QString::fromUtf8("verticalLayout_9"));
        splitter = new QSplitter(tab_2);
        splitter->setObjectName(QString::fromUtf8("splitter"));
        splitter->setOrientation(Qt::Horizontal);
        splitter->setChildrenCollapsible(false);
        layoutWidget = new QWidget(splitter);
        layoutWidget->setObjectName(QString::fromUtf8("layoutWidget"));
        verticalLayout_6 = new QVBoxLayout(layoutWidget);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        verticalLayout_6->setContentsMargins(0, 0, 10, 0);
        label_6 = new QLabel(layoutWidget);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        label_6->setWordWrap(true);

        verticalLayout_6->addWidget(label_6);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        label_3 = new QLabel(layoutWidget);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        horizontalLayout_7->addWidget(label_3);

        lineEditTab2Filter = new QLineEdit(layoutWidget);
        lineEditTab2Filter->setObjectName(QString::fromUtf8("lineEditTab2Filter"));
        lineEditTab2Filter->setDragEnabled(true);

        horizontalLayout_7->addWidget(lineEditTab2Filter);


        verticalLayout_6->addLayout(horizontalLayout_7);

        horizontalLayout_10 = new QHBoxLayout();
        horizontalLayout_10->setObjectName(QString::fromUtf8("horizontalLayout_10"));
        horizontalLayout_10->setContentsMargins(-1, -1, -1, 8);
        radioButtonContains = new QRadioButton(layoutWidget);
        radioButtonContains->setObjectName(QString::fromUtf8("radioButtonContains"));

        horizontalLayout_10->addWidget(radioButtonContains);

        radioButtonWildcard = new QRadioButton(layoutWidget);
        radioButtonWildcard->setObjectName(QString::fromUtf8("radioButtonWildcard"));
        radioButtonWildcard->setChecked(true);

        horizontalLayout_10->addWidget(radioButtonWildcard);

        radioButtonRegExp = new QRadioButton(layoutWidget);
        radioButtonRegExp->setObjectName(QString::fromUtf8("radioButtonRegExp"));

        horizontalLayout_10->addWidget(radioButtonRegExp);

        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_10->addItem(horizontalSpacer_5);


        verticalLayout_6->addLayout(horizontalLayout_10);

        listBatchSources = new QListWidget(layoutWidget);
        listBatchSources->setObjectName(QString::fromUtf8("listBatchSources"));

        verticalLayout_6->addWidget(listBatchSources);

        splitter->addWidget(layoutWidget);
        rightWidget_2 = new QWidget(splitter);
        rightWidget_2->setObjectName(QString::fromUtf8("rightWidget_2"));
        rightWidget_2->setMinimumSize(QSize(295, 0));
        verticalLayout_7 = new QVBoxLayout(rightWidget_2);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        verticalLayout_7->setContentsMargins(10, 0, 0, 0);
        label_8 = new QLabel(rightWidget_2);
        label_8->setObjectName(QString::fromUtf8("label_8"));
        label_8->setWordWrap(true);

        verticalLayout_7->addWidget(label_8);

        line = new QFrame(rightWidget_2);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShadow(QFrame::Plain);
        line->setFrameShape(QFrame::HLine);

        verticalLayout_7->addWidget(line);

        horizontalLayout_8 = new QHBoxLayout();
        horizontalLayout_8->setObjectName(QString::fromUtf8("horizontalLayout_8"));
        radioButtonPrefix = new QRadioButton(rightWidget_2);
        radioButtonPrefix->setObjectName(QString::fromUtf8("radioButtonPrefix"));

        horizontalLayout_8->addWidget(radioButtonPrefix);

        radioButtonSuffix = new QRadioButton(rightWidget_2);
        radioButtonSuffix->setObjectName(QString::fromUtf8("radioButtonSuffix"));
        radioButtonSuffix->setChecked(true);

        horizontalLayout_8->addWidget(radioButtonSuffix);

        suffixLineEdit = new QLineEdit(rightWidget_2);
        suffixLineEdit->setObjectName(QString::fromUtf8("suffixLineEdit"));
        sizePolicy.setHeightForWidth(suffixLineEdit->sizePolicy().hasHeightForWidth());
        suffixLineEdit->setSizePolicy(sizePolicy);
        suffixLineEdit->setMinimumSize(QSize(0, 25));
        suffixLineEdit->setFocusPolicy(Qt::ClickFocus);

        horizontalLayout_8->addWidget(suffixLineEdit);

        labelSemaphoreBatch = new QLabel(rightWidget_2);
        labelSemaphoreBatch->setObjectName(QString::fromUtf8("labelSemaphoreBatch"));
        labelSemaphoreBatch->setMinimumSize(QSize(30, 30));
        labelSemaphoreBatch->setMaximumSize(QSize(30, 30));

        horizontalLayout_8->addWidget(labelSemaphoreBatch);

        pushButtonHelpTab2 = new QPushButton(rightWidget_2);
        pushButtonHelpTab2->setObjectName(QString::fromUtf8("pushButtonHelpTab2"));
        pushButtonHelpTab2->setMaximumSize(QSize(50, 16777215));

        horizontalLayout_8->addWidget(pushButtonHelpTab2);

        horizontalLayout_8->setStretch(2, 1);

        verticalLayout_7->addLayout(horizontalLayout_8);

        horizontalLayout_9 = new QHBoxLayout();
        horizontalLayout_9->setObjectName(QString::fromUtf8("horizontalLayout_9"));
        horizontalLayout_9->setContentsMargins(-1, 11, -1, -1);
        label_4 = new QLabel(rightWidget_2);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setWordWrap(true);
        label_4->setOpenExternalLinks(true);

        horizontalLayout_9->addWidget(label_4);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_9->addItem(horizontalSpacer_4);


        verticalLayout_7->addLayout(horizontalLayout_9);

        widget_3 = new QWidget(rightWidget_2);
        widget_3->setObjectName(QString::fromUtf8("widget_3"));
        verticalLayout_8 = new QVBoxLayout(widget_3);
        verticalLayout_8->setObjectName(QString::fromUtf8("verticalLayout_8"));
        verticalLayout_8->setContentsMargins(0, 0, 0, 0);
        globalVarsTextBatch = new QCodeEditor(widget_3);
        globalVarsTextBatch->setObjectName(QString::fromUtf8("globalVarsTextBatch"));
        sizePolicy1.setHeightForWidth(globalVarsTextBatch->sizePolicy().hasHeightForWidth());
        globalVarsTextBatch->setSizePolicy(sizePolicy1);
        globalVarsTextBatch->setMinimumSize(QSize(0, 0));

        verticalLayout_8->addWidget(globalVarsTextBatch);

        labelFunction_2 = new QLabel(widget_3);
        labelFunction_2->setObjectName(QString::fromUtf8("labelFunction_2"));

        verticalLayout_8->addWidget(labelFunction_2);

        functionTextBatch = new QCodeEditor(widget_3);
        functionTextBatch->setObjectName(QString::fromUtf8("functionTextBatch"));
        functionTextBatch->setMinimumSize(QSize(0, 0));

        verticalLayout_8->addWidget(functionTextBatch);


        verticalLayout_7->addWidget(widget_3);

        splitter->addWidget(rightWidget_2);

        verticalLayout_9->addWidget(splitter);

        tabWidget->addTab(tab_2, QString());

        verticalLayout_3->addWidget(tabWidget);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        pushButtonCreate = new QPushButton(FunctionEditor);
        pushButtonCreate->setObjectName(QString::fromUtf8("pushButtonCreate"));
        pushButtonCreate->setEnabled(false);
        pushButtonCreate->setFocusPolicy(Qt::NoFocus);

        horizontalLayout->addWidget(pushButtonCreate);

        pushButtonCancel = new QPushButton(FunctionEditor);
        pushButtonCancel->setObjectName(QString::fromUtf8("pushButtonCancel"));
        pushButtonCancel->setFocusPolicy(Qt::NoFocus);
        pushButtonCancel->setAutoDefault(true);

        horizontalLayout->addWidget(pushButtonCancel);


        verticalLayout_3->addLayout(horizontalLayout);


        retranslateUi(FunctionEditor);

        tabWidget->setCurrentIndex(0);
        pushButtonCreate->setDefault(true);


        QMetaObject::connectSlotsByName(FunctionEditor);
    } // setupUi

    void retranslateUi(QWidget *FunctionEditor)
    {
        FunctionEditor->setWindowTitle(QCoreApplication::translate("FunctionEditor", "Function editor", nullptr));
#if QT_CONFIG(tooltip)
        label_linkeChannel->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>The timeseries that provides the (time,value) pairs to compute the funtion <span style=\" font-weight:600;\">calc(time,value)</span></p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        label_linkeChannel->setText(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Input timeseries. Provides arguments <span style=\" font-weight:600;\">time </span>and<span style=\" font-weight:600;\"> value</span>: </p></body></html>", nullptr));
        lineEditSource->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "drag & drop here the input timeseries", nullptr));
#if QT_CONFIG(tooltip)
        label_2->setToolTip(QString());
#endif // QT_CONFIG(tooltip)
        label_2->setText(QCoreApplication::translate("FunctionEditor", "Additional source timeseries:", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonDeleteCurves->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Remove selected time series.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonDeleteCurves->setText(QString());
#if QT_CONFIG(tooltip)
        listAdditionalSources->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Drag &amp; Drop your additional timeseries here.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        label_5->setText(QCoreApplication::translate("FunctionEditor", "Function library:", nullptr));
#if QT_CONFIG(tooltip)
        buttonLoadFunctions->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Import functions from a XML library.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonLoadFunctions->setText(QString());
#if QT_CONFIG(tooltip)
        buttonSaveFunctions->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Export functions from a XML library.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonSaveFunctions->setText(QString());
#if QT_CONFIG(tooltip)
        buttonSaveCurrent->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Save the current function in the library.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        buttonSaveCurrent->setText(QString());
#if QT_CONFIG(tooltip)
        snippetsListSaved->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>To load this function, <span style=\" font-weight:600;\">double-click</span> on it.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        label_7->setText(QCoreApplication::translate("FunctionEditor", "Function Preview:", nullptr));
#if QT_CONFIG(tooltip)
        label_name->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Name of the new time series.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        label_name->setText(QCoreApplication::translate("FunctionEditor", "New name:", nullptr));
        nameLineEdit->setText(QString());
        nameLineEdit->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "name the new timeseries", nullptr));
        labelSemaphore->setText(QString());
        label->setText(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Global variables</p></body></html>", nullptr));
        pushButtonHelp->setText(QCoreApplication::translate("FunctionEditor", "Help", nullptr));
#if QT_CONFIG(tooltip)
        globalVarsText->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Portion of code outside the function.</p><p>Usefull to add global variables.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        globalVarsText->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "Add your global variables here", nullptr));
        labelFunction->setText(QCoreApplication::translate("FunctionEditor", "function( time, value )", nullptr));
#if QT_CONFIG(tooltip)
        functionText->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Write your function implementation here. </p><p>It <span style=\" font-weight:600;\">must</span> return a new value. </p><p>The time used in the new time series will be the same.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        functionText->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "Write your function here", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab), QCoreApplication::translate("FunctionEditor", "Single Function", nullptr));
        label_6->setText(QCoreApplication::translate("FunctionEditor", "Input time series. Select them using the filter box.", nullptr));
        label_3->setText(QCoreApplication::translate("FunctionEditor", "Filter:", nullptr));
        radioButtonContains->setText(QCoreApplication::translate("FunctionEditor", "Contains", nullptr));
        radioButtonWildcard->setText(QCoreApplication::translate("FunctionEditor", "Wildcard", nullptr));
        radioButtonRegExp->setText(QCoreApplication::translate("FunctionEditor", "RegExp", nullptr));
        label_8->setText(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Apply the Lua function below  to <span style=\" font-weight:600;\">all </span>the series on the left table.<br/>Specify the prefix/suffix to be added to the original name.</p></body></html>", nullptr));
        radioButtonPrefix->setText(QCoreApplication::translate("FunctionEditor", "Prefix", nullptr));
        radioButtonSuffix->setText(QCoreApplication::translate("FunctionEditor", "Suffix", nullptr));
        suffixLineEdit->setText(QString());
        suffixLineEdit->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "prefix / suffix to add to the names", nullptr));
        labelSemaphoreBatch->setText(QString());
        pushButtonHelpTab2->setText(QCoreApplication::translate("FunctionEditor", "Help", nullptr));
        label_4->setText(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Global variables</p></body></html>", nullptr));
#if QT_CONFIG(tooltip)
        globalVarsTextBatch->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Portion of code outside the function.</p><p>Usefull to add global variables.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        globalVarsTextBatch->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "Add your global variables here", nullptr));
        labelFunction_2->setText(QCoreApplication::translate("FunctionEditor", "function( time, value )", nullptr));
#if QT_CONFIG(tooltip)
        functionTextBatch->setToolTip(QCoreApplication::translate("FunctionEditor", "<html><head/><body><p>Write your function implementation here. </p><p>It <span style=\" font-weight:600;\">must</span> return a new value. </p><p>The time used in the new time series will be the same.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        functionTextBatch->setPlaceholderText(QCoreApplication::translate("FunctionEditor", "Write your function here", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QCoreApplication::translate("FunctionEditor", "Batch Functions", nullptr));
        pushButtonCreate->setText(QCoreApplication::translate("FunctionEditor", "Create New Time Series", nullptr));
        pushButtonCancel->setText(QCoreApplication::translate("FunctionEditor", "Close", nullptr));
    } // retranslateUi

};

namespace Ui {
    class FunctionEditor: public Ui_FunctionEditor {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_FUNCTION_EDITOR_H
