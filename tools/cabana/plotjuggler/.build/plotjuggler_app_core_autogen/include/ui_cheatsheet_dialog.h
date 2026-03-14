/********************************************************************************
** Form generated from reading UI file 'cheatsheet_dialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.13
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CHEATSHEET_DIALOG_H
#define UI_CHEATSHEET_DIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_CheatsheetDialog
{
public:
    QVBoxLayout *verticalLayout_3;
    QHBoxLayout *horizontalLayout;
    QListWidget *listWidget;
    QStackedWidget *stackedWidget;
    QWidget *page_1;
    QVBoxLayout *verticalLayout;
    QTextBrowser *textBrowser;
    QLabel *labelImage_1;
    QWidget *page_2;
    QVBoxLayout *verticalLayout_2;
    QTextBrowser *textBrowser_2;
    QLabel *labelImage_2;
    QWidget *page_3;
    QVBoxLayout *verticalLayout_4;
    QTextBrowser *textBrowser_3;
    QLabel *labelImage_3;
    QWidget *page_4;
    QVBoxLayout *verticalLayout_5;
    QTextBrowser *textBrowser_4;
    QLabel *labelImage_4;
    QWidget *page_5;
    QVBoxLayout *verticalLayout_6;
    QTextBrowser *textBrowser_5;
    QLabel *labelImage_5;
    QWidget *page_6;
    QVBoxLayout *verticalLayout_7;
    QTextBrowser *textBrowser_6;
    QLabel *labelImage_6;
    QWidget *page_7;
    QVBoxLayout *verticalLayout_8;
    QTextBrowser *textBrowser_7;
    QLabel *labelImage_7;
    QWidget *page_8;
    QVBoxLayout *verticalLayout_9;
    QTextBrowser *textBrowser_8;
    QLabel *labelImage_8;
    QWidget *page_9;
    QVBoxLayout *verticalLayout_10;
    QTextBrowser *textBrowser_9;
    QLabel *labelImage_9;
    QWidget *page_10;
    QVBoxLayout *verticalLayout_11;
    QTextBrowser *textBrowser_10;
    QLabel *labelImage_10;
    QWidget *page_11;
    QVBoxLayout *verticalLayout_12;
    QTextBrowser *textBrowser_11;
    QLabel *labelImage_11;

    void setupUi(QDialog *CheatsheetDialog)
    {
        if (CheatsheetDialog->objectName().isEmpty())
            CheatsheetDialog->setObjectName(QString::fromUtf8("CheatsheetDialog"));
        CheatsheetDialog->resize(1231, 831);
        verticalLayout_3 = new QVBoxLayout(CheatsheetDialog);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        listWidget = new QListWidget(CheatsheetDialog);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        new QListWidgetItem(listWidget);
        listWidget->setObjectName(QString::fromUtf8("listWidget"));
        listWidget->setMinimumSize(QSize(200, 0));
        listWidget->setMaximumSize(QSize(200, 16777215));
        QFont font;
        font.setPointSize(11);
        listWidget->setFont(font);
        listWidget->setSpacing(2);
        listWidget->setGridSize(QSize(196, 22));

        horizontalLayout->addWidget(listWidget);

        stackedWidget = new QStackedWidget(CheatsheetDialog);
        stackedWidget->setObjectName(QString::fromUtf8("stackedWidget"));
        stackedWidget->setMinimumSize(QSize(1000, 800));
        page_1 = new QWidget();
        page_1->setObjectName(QString::fromUtf8("page_1"));
        verticalLayout = new QVBoxLayout(page_1);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        textBrowser = new QTextBrowser(page_1);
        textBrowser->setObjectName(QString::fromUtf8("textBrowser"));
        textBrowser->setMinimumSize(QSize(0, 200));

        verticalLayout->addWidget(textBrowser);

        labelImage_1 = new QLabel(page_1);
        labelImage_1->setObjectName(QString::fromUtf8("labelImage_1"));
        labelImage_1->setMinimumSize(QSize(900, 600));
        labelImage_1->setScaledContents(false);
        labelImage_1->setAlignment(Qt::AlignCenter);

        verticalLayout->addWidget(labelImage_1);

        verticalLayout->setStretch(1, 1);
        stackedWidget->addWidget(page_1);
        page_2 = new QWidget();
        page_2->setObjectName(QString::fromUtf8("page_2"));
        verticalLayout_2 = new QVBoxLayout(page_2);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        textBrowser_2 = new QTextBrowser(page_2);
        textBrowser_2->setObjectName(QString::fromUtf8("textBrowser_2"));
        textBrowser_2->setMinimumSize(QSize(0, 250));

        verticalLayout_2->addWidget(textBrowser_2);

        labelImage_2 = new QLabel(page_2);
        labelImage_2->setObjectName(QString::fromUtf8("labelImage_2"));
        labelImage_2->setMinimumSize(QSize(900, 600));
        labelImage_2->setScaledContents(false);
        labelImage_2->setAlignment(Qt::AlignCenter);

        verticalLayout_2->addWidget(labelImage_2);

        verticalLayout_2->setStretch(1, 1);
        stackedWidget->addWidget(page_2);
        page_3 = new QWidget();
        page_3->setObjectName(QString::fromUtf8("page_3"));
        verticalLayout_4 = new QVBoxLayout(page_3);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        verticalLayout_4->setContentsMargins(0, 0, 0, 0);
        textBrowser_3 = new QTextBrowser(page_3);
        textBrowser_3->setObjectName(QString::fromUtf8("textBrowser_3"));
        textBrowser_3->setMinimumSize(QSize(0, 200));

        verticalLayout_4->addWidget(textBrowser_3);

        labelImage_3 = new QLabel(page_3);
        labelImage_3->setObjectName(QString::fromUtf8("labelImage_3"));
        labelImage_3->setMinimumSize(QSize(0, 600));
        labelImage_3->setScaledContents(false);
        labelImage_3->setAlignment(Qt::AlignCenter);

        verticalLayout_4->addWidget(labelImage_3);

        verticalLayout_4->setStretch(1, 1);
        stackedWidget->addWidget(page_3);
        page_4 = new QWidget();
        page_4->setObjectName(QString::fromUtf8("page_4"));
        verticalLayout_5 = new QVBoxLayout(page_4);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        verticalLayout_5->setContentsMargins(0, 0, 0, 0);
        textBrowser_4 = new QTextBrowser(page_4);
        textBrowser_4->setObjectName(QString::fromUtf8("textBrowser_4"));
        textBrowser_4->setMinimumSize(QSize(0, 200));

        verticalLayout_5->addWidget(textBrowser_4);

        labelImage_4 = new QLabel(page_4);
        labelImage_4->setObjectName(QString::fromUtf8("labelImage_4"));
        labelImage_4->setMinimumSize(QSize(900, 600));
        labelImage_4->setScaledContents(false);
        labelImage_4->setAlignment(Qt::AlignCenter);

        verticalLayout_5->addWidget(labelImage_4);

        verticalLayout_5->setStretch(1, 1);
        stackedWidget->addWidget(page_4);
        page_5 = new QWidget();
        page_5->setObjectName(QString::fromUtf8("page_5"));
        verticalLayout_6 = new QVBoxLayout(page_5);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        verticalLayout_6->setContentsMargins(0, 0, 0, 0);
        textBrowser_5 = new QTextBrowser(page_5);
        textBrowser_5->setObjectName(QString::fromUtf8("textBrowser_5"));
        textBrowser_5->setMinimumSize(QSize(0, 250));

        verticalLayout_6->addWidget(textBrowser_5);

        labelImage_5 = new QLabel(page_5);
        labelImage_5->setObjectName(QString::fromUtf8("labelImage_5"));
        labelImage_5->setMinimumSize(QSize(900, 600));
        labelImage_5->setScaledContents(false);
        labelImage_5->setAlignment(Qt::AlignCenter);

        verticalLayout_6->addWidget(labelImage_5);

        verticalLayout_6->setStretch(1, 1);
        stackedWidget->addWidget(page_5);
        page_6 = new QWidget();
        page_6->setObjectName(QString::fromUtf8("page_6"));
        verticalLayout_7 = new QVBoxLayout(page_6);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        verticalLayout_7->setContentsMargins(0, 0, 0, 0);
        textBrowser_6 = new QTextBrowser(page_6);
        textBrowser_6->setObjectName(QString::fromUtf8("textBrowser_6"));
        textBrowser_6->setMinimumSize(QSize(0, 250));

        verticalLayout_7->addWidget(textBrowser_6);

        labelImage_6 = new QLabel(page_6);
        labelImage_6->setObjectName(QString::fromUtf8("labelImage_6"));
        labelImage_6->setMinimumSize(QSize(900, 600));
        labelImage_6->setScaledContents(false);
        labelImage_6->setAlignment(Qt::AlignCenter);

        verticalLayout_7->addWidget(labelImage_6);

        verticalLayout_7->setStretch(1, 1);
        stackedWidget->addWidget(page_6);
        page_7 = new QWidget();
        page_7->setObjectName(QString::fromUtf8("page_7"));
        verticalLayout_8 = new QVBoxLayout(page_7);
        verticalLayout_8->setObjectName(QString::fromUtf8("verticalLayout_8"));
        textBrowser_7 = new QTextBrowser(page_7);
        textBrowser_7->setObjectName(QString::fromUtf8("textBrowser_7"));
        textBrowser_7->setMinimumSize(QSize(0, 150));

        verticalLayout_8->addWidget(textBrowser_7);

        labelImage_7 = new QLabel(page_7);
        labelImage_7->setObjectName(QString::fromUtf8("labelImage_7"));
        labelImage_7->setMinimumSize(QSize(900, 600));
        labelImage_7->setScaledContents(false);
        labelImage_7->setAlignment(Qt::AlignCenter);

        verticalLayout_8->addWidget(labelImage_7);

        stackedWidget->addWidget(page_7);
        page_8 = new QWidget();
        page_8->setObjectName(QString::fromUtf8("page_8"));
        verticalLayout_9 = new QVBoxLayout(page_8);
        verticalLayout_9->setObjectName(QString::fromUtf8("verticalLayout_9"));
        verticalLayout_9->setContentsMargins(0, 0, 0, 0);
        textBrowser_8 = new QTextBrowser(page_8);
        textBrowser_8->setObjectName(QString::fromUtf8("textBrowser_8"));
        textBrowser_8->setMinimumSize(QSize(0, 250));

        verticalLayout_9->addWidget(textBrowser_8);

        labelImage_8 = new QLabel(page_8);
        labelImage_8->setObjectName(QString::fromUtf8("labelImage_8"));
        labelImage_8->setMinimumSize(QSize(900, 600));
        labelImage_8->setAlignment(Qt::AlignCenter);

        verticalLayout_9->addWidget(labelImage_8);

        stackedWidget->addWidget(page_8);
        page_9 = new QWidget();
        page_9->setObjectName(QString::fromUtf8("page_9"));
        verticalLayout_10 = new QVBoxLayout(page_9);
        verticalLayout_10->setObjectName(QString::fromUtf8("verticalLayout_10"));
        verticalLayout_10->setContentsMargins(0, 0, 0, 0);
        textBrowser_9 = new QTextBrowser(page_9);
        textBrowser_9->setObjectName(QString::fromUtf8("textBrowser_9"));
        textBrowser_9->setMinimumSize(QSize(0, 250));

        verticalLayout_10->addWidget(textBrowser_9);

        labelImage_9 = new QLabel(page_9);
        labelImage_9->setObjectName(QString::fromUtf8("labelImage_9"));
        labelImage_9->setMinimumSize(QSize(900, 600));
        labelImage_9->setScaledContents(false);
        labelImage_9->setAlignment(Qt::AlignCenter);

        verticalLayout_10->addWidget(labelImage_9);

        verticalLayout_10->setStretch(1, 1);
        stackedWidget->addWidget(page_9);
        page_10 = new QWidget();
        page_10->setObjectName(QString::fromUtf8("page_10"));
        verticalLayout_11 = new QVBoxLayout(page_10);
        verticalLayout_11->setObjectName(QString::fromUtf8("verticalLayout_11"));
        verticalLayout_11->setContentsMargins(0, 0, 0, 0);
        textBrowser_10 = new QTextBrowser(page_10);
        textBrowser_10->setObjectName(QString::fromUtf8("textBrowser_10"));
        textBrowser_10->setMinimumSize(QSize(0, 150));
        textBrowser_10->setMaximumSize(QSize(16777215, 16777215));

        verticalLayout_11->addWidget(textBrowser_10);

        labelImage_10 = new QLabel(page_10);
        labelImage_10->setObjectName(QString::fromUtf8("labelImage_10"));
        labelImage_10->setMinimumSize(QSize(900, 600));
        labelImage_10->setScaledContents(false);
        labelImage_10->setAlignment(Qt::AlignCenter);

        verticalLayout_11->addWidget(labelImage_10);

        verticalLayout_11->setStretch(1, 1);
        stackedWidget->addWidget(page_10);
        page_11 = new QWidget();
        page_11->setObjectName(QString::fromUtf8("page_11"));
        verticalLayout_12 = new QVBoxLayout(page_11);
        verticalLayout_12->setObjectName(QString::fromUtf8("verticalLayout_12"));
        textBrowser_11 = new QTextBrowser(page_11);
        textBrowser_11->setObjectName(QString::fromUtf8("textBrowser_11"));
        textBrowser_11->setMinimumSize(QSize(0, 200));

        verticalLayout_12->addWidget(textBrowser_11);

        labelImage_11 = new QLabel(page_11);
        labelImage_11->setObjectName(QString::fromUtf8("labelImage_11"));
        labelImage_11->setScaledContents(true);

        verticalLayout_12->addWidget(labelImage_11);

        verticalLayout_12->setStretch(1, 1);
        stackedWidget->addWidget(page_11);

        horizontalLayout->addWidget(stackedWidget);

        horizontalLayout->setStretch(1, 2);

        verticalLayout_3->addLayout(horizontalLayout);


        retranslateUi(CheatsheetDialog);

        stackedWidget->setCurrentIndex(10);


        QMetaObject::connectSlotsByName(CheatsheetDialog);
    } // setupUi

    void retranslateUi(QDialog *CheatsheetDialog)
    {
        CheatsheetDialog->setWindowTitle(QCoreApplication::translate("CheatsheetDialog", "Tips and tricks", nullptr));

        const bool __sortingEnabled = listWidget->isSortingEnabled();
        listWidget->setSortingEnabled(false);
        QListWidgetItem *___qlistwidgetitem = listWidget->item(0);
        ___qlistwidgetitem->setText(QCoreApplication::translate("CheatsheetDialog", "Visualize a Curve", nullptr));
        QListWidgetItem *___qlistwidgetitem1 = listWidget->item(1);
        ___qlistwidgetitem1->setText(QCoreApplication::translate("CheatsheetDialog", "Find Curves by name", nullptr));
        QListWidgetItem *___qlistwidgetitem2 = listWidget->item(2);
        ___qlistwidgetitem2->setText(QCoreApplication::translate("CheatsheetDialog", "Multiple Plotting Areas", nullptr));
        QListWidgetItem *___qlistwidgetitem3 = listWidget->item(3);
        ___qlistwidgetitem3->setText(QCoreApplication::translate("CheatsheetDialog", "Create XY Curves", nullptr));
        QListWidgetItem *___qlistwidgetitem4 = listWidget->item(4);
        ___qlistwidgetitem4->setText(QCoreApplication::translate("CheatsheetDialog", "Time tracker", nullptr));
        QListWidgetItem *___qlistwidgetitem5 = listWidget->item(5);
        ___qlistwidgetitem5->setText(QCoreApplication::translate("CheatsheetDialog", "Zoom", nullptr));
        QListWidgetItem *___qlistwidgetitem6 = listWidget->item(6);
        ___qlistwidgetitem6->setText(QCoreApplication::translate("CheatsheetDialog", "Resize Fonts", nullptr));
        QListWidgetItem *___qlistwidgetitem7 = listWidget->item(7);
        ___qlistwidgetitem7->setText(QCoreApplication::translate("CheatsheetDialog", "Filters/Transforms", nullptr));
        QListWidgetItem *___qlistwidgetitem8 = listWidget->item(8);
        ___qlistwidgetitem8->setText(QCoreApplication::translate("CheatsheetDialog", "Advanced Transforms", nullptr));
        QListWidgetItem *___qlistwidgetitem9 = listWidget->item(9);
        ___qlistwidgetitem9->setText(QCoreApplication::translate("CheatsheetDialog", "Layout Files", nullptr));
        QListWidgetItem *___qlistwidgetitem10 = listWidget->item(10);
        ___qlistwidgetitem10->setText(QCoreApplication::translate("CheatsheetDialog", "Background color", nullptr));
        listWidget->setSortingEnabled(__sortingEnabled);

        textBrowser->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Visualize a Curve</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">On the left size you have a list of available time series / curves.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; te"
                        "xt-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Drag and Drop into the Plotting Area using the <span style=\" font-weight:600;\">LEFT Mouse</span> button. You may select more than one curve using keeping the CTRL key pressed.</p></body></html>", nullptr));
        labelImage_1->setText(QString());
        textBrowser_2->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Select/Find a curve</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Hundreds or thousands of timeseries? The search filter on the left side can help you find easily one or more curve.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0p"
                        "x; -qt-block-indent:0; text-indent:0px;\">Use one or more chunks of text that the name of the curve must contain.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">For instance, the search filter <span style=\" font-weight:600;\">&quot;pos x</span>&quot; will select:</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">-  /turtle1/<span style=\" font-weight:600;\">pos</span>e/<span style=\" font-weight:600;\">x</span></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;"
                        "\">-  /turtle2/<span style=\" font-weight:600;\">pos</span>e/<span style=\" font-weight:600;\">x</span></p></body></html>", nullptr));
        labelImage_2->setText(QString());
        textBrowser_3->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Multiple Plotting Areas</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">PlotJuggler allows you to have multiple Plotting Areas.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-ind"
                        "ent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">- You can split an Area vertically or horizontally (<span style=\" font-weight:600;\">A</span>)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">- You may create multiple tabs (<span style=\" font-weight:600;\">B</span>).</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">- You can temporary see a single Area in &quot;fullscreen&quot; mode (<span style=\" font-weight:600;\">C</span>)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">- Areas can be moved and re-arranged. Drag and drop on its title to move it (<span style=\" font-weight:600;\">D</span>)</p></body></html>", nullptr));
        labelImage_3->setText(QString());
        textBrowser_4->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Create a XY Curve</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">PlotJuggler is mainly used to visualize timeseries, i.e. plots where the X axis represent time.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right"
                        ":0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">You can combine two timeseries into a XY curve, but only when they share  the same time axis. </p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Select two curves (keeping the CTRL key pressed) and Drag &amp; Drop them using the<span style=\" font-weight:600;\"> RIGHT Mouse</span> button.</p></body></html>", nullptr));
        labelImage_4->setText(QString());
        textBrowser_5->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">The time tracker</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">The time tracker is a vertical line that is used to see the value of the curves across Plotting Areas (<span style=\" font-weight:600;\">A</span>). Use button <span style=\" font-weight:600;\">B</span> to c"
                        "ustomize the amount of information shown. </p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">You can move the time tracker using the time slider at the bottom or pressing <span style=\" font-weight:600;\">[SHIFT + Left Mouse]</span> on any plot.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">The purpose of the time tracker is not limited to visualization: in fact, its position triggers the so called &quot;Statepublishers&quot; (<span style=\" font-weight:600;\">C</span>), which will react to the motion of the tracker. Use the play butto"
                        "n (<span style=\" font-weight:600;\">D</span>) to animate the tracker.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p></body></html>", nullptr));
        labelImage_5->setText(QString());
        textBrowser_6->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Zoom</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">There are multiple way to zoom the Plotting Area:</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
""
                        "<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">1) LEFT click and select a rectangle (<span style=\" font-weight:600;\">A</span>)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">2) Use the mouse wheel to zoom in/out.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">3) Zoom out completely pusling the button <span style=\" font-weight:600;\">B</span>.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">4) Synchronize the zoom of multiple Areas using the &quot;link&quot; button (<span style=\" font-weight:600;\">C</span>).</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">5) If the mouse cursor is on top of one of the axes (<span "
                        "style=\" font-weight:600;\">D1/D2</span>) you may use the Mouse Wheel to zoom only vertically/horizontally.</p></body></html>", nullptr));
        labelImage_6->setText(QString());
        textBrowser_7->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Resize Fonts</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Use <span style=\" font-weight:600;\">CTRL + Mouse Wheel</span> to resize the size of the fonts in the Curves list or the Legend.</p></body></html>", nullptr));
        labelImage_7->setText(QString());
        textBrowser_8->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Apply filters and simple timeseries transforms</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">PlotJuggler provides an intuitive way to apply transformations to your data using a simple graphical interface. For instance you can apply:</p>\n"
"<p style=\"-qt-paragraph-typ"
                        "e:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">    - A moving average filter.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">    - A first order derivative.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">    - Calculate the integral.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">    - An outlier removal.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">    - Add your own using plugins!</p></body></html>", nullptr));
        labelImage_8->setText(QString());
        textBrowser_9->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Advanced tansforms: create a custom timeseries</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:12pt; font-weight:600;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">You can create a<span style=\" font-weight:600;\"> Multiple-Inputs / Single Output</span> function using existing timeseries.</p>\n"
"<p style="
                        "\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">The scripting language is based on Lua, a programming language that will take you 5 minutes to understand ;)</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">    A</span>) Main timeseries that provid the time points and a value (mandatory)</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">    B</span>) Additional data source for multi-inputs formulas (optional).</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;"
                        "\">    C</span>) The main function called to create a data point.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">    D</span>) Global context to initialize global variables.</p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-weight:600;\">    E</span>) Semaphore: <span style=\" color:#cc0000;\">red</span> if anything is wrong/missing. </p></body></html>", nullptr));
        labelImage_9->setText(QString());
        textBrowser_10->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Save/Load the Layout</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:12pt; font-weight:600;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Once you carefully crafted your curves, transforms, Plotting Areas and Tabs, you can save to file the entire layour and load it later.</p>\n"
"<p style=\"-qt-paragraph-t"
                        "ype:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Furthermore... do you know that <span style=\" font-weight:600;\">CTRL+Z </span>(undo) and <span style=\" font-weight:600;\">CTRL+SHIF+Z </span>(redo) works too?</p></body></html>", nullptr));
        labelImage_10->setText(QString());
        textBrowser_11->setHtml(QCoreApplication::translate("CheatsheetDialog", "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
"p, li { white-space: pre-wrap; }\n"
"</style></head><body style=\" font-family:'Ubuntu'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><span style=\" font-size:12pt; font-weight:600;\">Contextual background color</span></p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:12pt; font-weight:600;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">You can change the color of the backgorund based on the value of a (single) time series.</p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-botto"
                        "m:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Drag and drop a curve onto the icon on the right-top corner and select/edit a ColorMap, expressed as a simple Lua script that returns a color. </p>\n"
"<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\"><br /></p>\n"
"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">Clickin with you mouse on the icon, you can modify or remove the colormap</p></body></html>", nullptr));
        labelImage_11->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class CheatsheetDialog: public Ui_CheatsheetDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CHEATSHEET_DIALOG_H
