/********************************************************************************
** Form generated from reading UI file 'lua_editor.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_LUA_EDITOR_H
#define UI_LUA_EDITOR_H

#include <QCodeEditor>
#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_LuaEditor
{
public:
    QVBoxLayout *verticalLayout_3;
    QLabel *label_6;
    QFrame *line;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_4;
    QHBoxLayout *horizontalLayout_2;
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QCodeEditor *textGlobal;
    QLabel *label_4;
    QLabel *label_2;
    QCodeEditor *textFunction;
    QVBoxLayout *verticalLayout_2;
    QHBoxLayout *horizontalLayout;
    QLabel *label_7;
    QLineEdit *lineEditFunctionName;
    QPushButton *pushButtonSave;
    QHBoxLayout *horizontalLayout_3;
    QLabel *label_3;
    QPushButton *pushButtonDelete;
    QListWidget *listWidgetFunctions;
    QLabel *label_5;
    QListWidget *listWidgetRecent;
    QWidget *tab_2;
    QVBoxLayout *verticalLayout_5;
    QHBoxLayout *horizontalLayout_4;
    QLabel *label_8;
    QLabel *labelSemaphore;
    QHBoxLayout *horizontalLayout_5;
    QPushButton *pushButtonDefaultLibrary;
    QPushButton *pushButtonApplyLibrary;
    QSpacerItem *horizontalSpacer;
    QCodeEditor *textLibrary;
    QDialogButtonBox *buttonBox;

    void setupUi(QWidget *LuaEditor)
    {
        if (LuaEditor->objectName().isEmpty())
            LuaEditor->setObjectName(QString::fromUtf8("LuaEditor"));
        LuaEditor->resize(1084, 627);
        verticalLayout_3 = new QVBoxLayout(LuaEditor);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        label_6 = new QLabel(LuaEditor);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        QFont font;
        font.setPointSize(12);
        label_6->setFont(font);
        label_6->setWordWrap(true);
        label_6->setOpenExternalLinks(true);

        verticalLayout_3->addWidget(label_6);

        line = new QFrame(LuaEditor);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShadow(QFrame::Plain);
        line->setFrameShape(QFrame::HLine);

        verticalLayout_3->addWidget(line);

        tabWidget = new QTabWidget(LuaEditor);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        tab = new QWidget();
        tab->setObjectName(QString::fromUtf8("tab"));
        verticalLayout_4 = new QVBoxLayout(tab);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        label = new QLabel(tab);
        label->setObjectName(QString::fromUtf8("label"));

        verticalLayout->addWidget(label);

        textGlobal = new QCodeEditor(tab);
        textGlobal->setObjectName(QString::fromUtf8("textGlobal"));
        textGlobal->setFont(font);

        verticalLayout->addWidget(textGlobal);

        label_4 = new QLabel(tab);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setWordWrap(true);

        verticalLayout->addWidget(label_4);

        label_2 = new QLabel(tab);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        QFont font1;
        font1.setBold(true);
        font1.setWeight(75);
        label_2->setFont(font1);

        verticalLayout->addWidget(label_2);

        textFunction = new QCodeEditor(tab);
        textFunction->setObjectName(QString::fromUtf8("textFunction"));
        textFunction->setFont(font);

        verticalLayout->addWidget(textFunction);

        verticalLayout->setStretch(1, 1);
        verticalLayout->setStretch(4, 2);

        horizontalLayout_2->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label_7 = new QLabel(tab);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        horizontalLayout->addWidget(label_7);

        lineEditFunctionName = new QLineEdit(tab);
        lineEditFunctionName->setObjectName(QString::fromUtf8("lineEditFunctionName"));

        horizontalLayout->addWidget(lineEditFunctionName);

        pushButtonSave = new QPushButton(tab);
        pushButtonSave->setObjectName(QString::fromUtf8("pushButtonSave"));
        pushButtonSave->setEnabled(false);

        horizontalLayout->addWidget(pushButtonSave);


        verticalLayout_2->addLayout(horizontalLayout);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        horizontalLayout_3->setContentsMargins(-1, 11, -1, -1);
        label_3 = new QLabel(tab);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        horizontalLayout_3->addWidget(label_3);

        pushButtonDelete = new QPushButton(tab);
        pushButtonDelete->setObjectName(QString::fromUtf8("pushButtonDelete"));
        pushButtonDelete->setEnabled(false);
        pushButtonDelete->setMinimumSize(QSize(28, 28));
        pushButtonDelete->setMaximumSize(QSize(28, 28));
        pushButtonDelete->setIconSize(QSize(25, 25));
        pushButtonDelete->setFlat(true);

        horizontalLayout_3->addWidget(pushButtonDelete);


        verticalLayout_2->addLayout(horizontalLayout_3);

        listWidgetFunctions = new QListWidget(tab);
        listWidgetFunctions->setObjectName(QString::fromUtf8("listWidgetFunctions"));

        verticalLayout_2->addWidget(listWidgetFunctions);

        label_5 = new QLabel(tab);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setWordWrap(true);

        verticalLayout_2->addWidget(label_5);

        listWidgetRecent = new QListWidget(tab);
        listWidgetRecent->setObjectName(QString::fromUtf8("listWidgetRecent"));

        verticalLayout_2->addWidget(listWidgetRecent);


        horizontalLayout_2->addLayout(verticalLayout_2);

        horizontalLayout_2->setStretch(0, 2);
        horizontalLayout_2->setStretch(1, 1);

        verticalLayout_4->addLayout(horizontalLayout_2);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName(QString::fromUtf8("tab_2"));
        verticalLayout_5 = new QVBoxLayout(tab_2);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalLayout_4->setContentsMargins(-1, 10, -1, -1);
        label_8 = new QLabel(tab_2);
        label_8->setObjectName(QString::fromUtf8("label_8"));
        label_8->setWordWrap(true);

        horizontalLayout_4->addWidget(label_8);

        labelSemaphore = new QLabel(tab_2);
        labelSemaphore->setObjectName(QString::fromUtf8("labelSemaphore"));
        labelSemaphore->setMinimumSize(QSize(32, 32));
        labelSemaphore->setMaximumSize(QSize(32, 32));

        horizontalLayout_4->addWidget(labelSemaphore);


        verticalLayout_5->addLayout(horizontalLayout_4);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalLayout_5->setContentsMargins(-1, 5, -1, -1);
        pushButtonDefaultLibrary = new QPushButton(tab_2);
        pushButtonDefaultLibrary->setObjectName(QString::fromUtf8("pushButtonDefaultLibrary"));
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(pushButtonDefaultLibrary->sizePolicy().hasHeightForWidth());
        pushButtonDefaultLibrary->setSizePolicy(sizePolicy);

        horizontalLayout_5->addWidget(pushButtonDefaultLibrary);

        pushButtonApplyLibrary = new QPushButton(tab_2);
        pushButtonApplyLibrary->setObjectName(QString::fromUtf8("pushButtonApplyLibrary"));
        pushButtonApplyLibrary->setEnabled(false);

        horizontalLayout_5->addWidget(pushButtonApplyLibrary);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer);


        verticalLayout_5->addLayout(horizontalLayout_5);

        textLibrary = new QCodeEditor(tab_2);
        textLibrary->setObjectName(QString::fromUtf8("textLibrary"));
        textLibrary->setFont(font);

        verticalLayout_5->addWidget(textLibrary);

        tabWidget->addTab(tab_2, QString());

        verticalLayout_3->addWidget(tabWidget);

        buttonBox = new QDialogButtonBox(LuaEditor);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout_3->addWidget(buttonBox);

        verticalLayout_3->setStretch(1, 1);

        retranslateUi(LuaEditor);

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(LuaEditor);
    } // setupUi

    void retranslateUi(QWidget *LuaEditor)
    {
        LuaEditor->setWindowTitle(QCoreApplication::translate("LuaEditor", "Form", nullptr));
        label_6->setText(QCoreApplication::translate("LuaEditor", "<html><head/><body><p>The <span style=\" font-weight:600;\">Script Editor </span>is an advanced Lua editor that allows the user to create new series (ScatterXY or Timeseries) which are updated when the timetracker slider is moved or new data is received. <a href=\"https://slides.com/davidefaconti/plotjuggler-reactive-scripts/fullscreen\"><span style=\" text-decoration: underline; color:#0000ff;\">Tutorial link</span></a>.</p></body></html>", nullptr));
        label->setText(QCoreApplication::translate("LuaEditor", "Global code, execute once:", nullptr));
        textGlobal->setPlaceholderText(QCoreApplication::translate("LuaEditor", "define here your global variables", nullptr));
        label_4->setText(QCoreApplication::translate("LuaEditor", "The following function is called every time the time tracker is moved or new data is received.", nullptr));
        label_2->setText(QCoreApplication::translate("LuaEditor", "function(tracker_time)", nullptr));
        textFunction->setPlaceholderText(QCoreApplication::translate("LuaEditor", "body of the function. tracker_time is the value of the time slider", nullptr));
        label_7->setText(QCoreApplication::translate("LuaEditor", "Name:", nullptr));
        pushButtonSave->setText(QCoreApplication::translate("LuaEditor", "Save", nullptr));
        label_3->setText(QCoreApplication::translate("LuaEditor", "Active Scripts:", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonDelete->setToolTip(QCoreApplication::translate("LuaEditor", "<html><head/><body><p>Remove selected script</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonDelete->setText(QString());
        label_5->setText(QCoreApplication::translate("LuaEditor", "<html><head/><body><p>Recent scripts (double-click to load):</p></body></html>", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab), QCoreApplication::translate("LuaEditor", "Script Editor", nullptr));
        label_8->setText(QCoreApplication::translate("LuaEditor", "Add here your helper functions, which can be used in your script. Useful to make your scripts less verbose.", nullptr));
        labelSemaphore->setText(QString());
        pushButtonDefaultLibrary->setText(QCoreApplication::translate("LuaEditor", "Restore default", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonApplyLibrary->setToolTip(QCoreApplication::translate("LuaEditor", "<html><head/><body><p>If you have already created one or more reactive series, reload them witrh the new library.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonApplyLibrary->setText(QCoreApplication::translate("LuaEditor", "Apply changes", nullptr));
        textLibrary->setHtml(QCoreApplication::translate("LuaEditor", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'MS Shell Dlg 2'; font-size:12pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">--[[ Helper function to create a series from arrays</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> new_series: a series previously created with ScatterXY.new(name)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-"
                        "state:1;\"> prefix:     prefix of the timeseries, before the index of the array</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> suffix_X:   suffix to complete the name of the series containing the X value. If [nil], use the index of the array.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> suffix_Y:   suffix to complete the name of the series containing the Y value</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> timestamp:   usually the tracker_time variable</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">              </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block"
                        "-indent:0; text-indent:0px; -qt-user-state:1;\"> Example:</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> Assuming we have multiple series in the form:</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">   /trajectory/node.{X}/position/x</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">   /trajectory/node.{X}/position/y</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; t"
                        "ext-indent:0px; -qt-user-state:1;\">   </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> where {N} is the index of the array (integer). We can create a reactive series from the array with:</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">   new_series = ScatterXY.new(&quot;my_trajectory&quot;) </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">   CreateSeriesFromArray( new_series, &quot;/trajectory/node&quot;, &quot;position/x&quot;, &quot;position/y&quot;, tracker_time );</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-inde"
                        "nt:0; text-indent:0px; -qt-user-state:0;\">--]]</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">function CreateSeriesFromArray( new_series, prefix, suffix_X, suffix_Y, timestamp )</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  --- clear previous values</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  new_series:clear()</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -q"
                        "t-block-indent:0; text-indent:0px; -qt-user-state:0;\">  </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  --- Append points to new_series</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  index = 0</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  while(true) do</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    x = index;</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-stat"
                        "e:0;\">    -- if not nil, get the X coordinate from a series</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    if suffix_X ~= nil then </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">      series_x = TimeseriesView.find( string.format( &quot;%s.%d/%s&quot;, prefix, index, suffix_X) )</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">      if series_x == nil then break end</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">      x = series_x:atTime(timestamp)	 </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    end</p>\n"
"<p "
                        "style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    series_y = TimeseriesView.find( string.format( &quot;%s.%d/%s&quot;, prefix, index, suffix_Y) )</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    if series_y == nil then break end </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    y = series_y:atTime(timestamp)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-in"
                        "dent:0px; -qt-user-state:0;\">    new_series:push_back(x,y)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    index = index+1</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  end</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">end</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">--[[ Similar to the built-in function GetSeriesNames(), but select only the names with a give prefix. --]]</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px;"
                        " margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">function GetSeriesNamesByPrefix(prefix)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  -- GetSeriesNames(9 is a built-in function</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  all_names = GetSeriesNames()</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  filtered_names = {}</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  for i, name in ipairs(all_names)  do</p>\n"
"<p style=\" margin-top:"
                        "0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    -- check the prefix</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    if name:find(prefix, 1, #prefix) then</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">      table.insert(filtered_names, name);</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    end</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  end</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  return filtered_names</p>\n"
"<p style=\" margin-t"
                        "op:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">end</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">--[[ Modify an existing series, applying offsets to all their X and Y values</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> series: an existing timeseries, obtained with TimeseriesView.find(name)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-"
                        "state:1;\"> delta_x: offset to apply to each x value</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\"> delta_y: offset to apply to each y value </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:1;\">  </p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">--]]</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">function ApplyOffsetInPlace(series, delta_x, delta_y)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-ind"
                        "ent:0px; -qt-user-state:0;\">  -- use C++ indeces, not Lua indeces</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  for index=0, series:size()-1 do</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    x,y = series:at(index)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">    series:set(index, x + delta_x, y + delta_y)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">  end</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; -qt-user-state:0;\">end</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-"
                        "right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p></body></html>", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QCoreApplication::translate("LuaEditor", "Function Library", nullptr));
    } // retranslateUi

};

namespace Ui {
    class LuaEditor: public Ui_LuaEditor {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_LUA_EDITOR_H
