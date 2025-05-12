/********************************************************************************
** Form generated from reading UI file 'Channel_sim.ui'
**
** Created by: Qt User Interface Compiler version 6.7.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CHANNEL_SIM_H
#define UI_CHANNEL_SIM_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Channel_simClass
{
public:
    QAction *file_action;
    QAction *actionchannel;
    QAction *actionlog;
    QAction *actionvideo;
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout;
    QMenuBar *menuBar;
    QMenu *menu;
    QMenu *view_action;
    QToolBar *mainToolBar;
    QDockWidget *dockWidget_4;
    QWidget *dockWidgetContents_4;
    QListWidget *listWidget;
    QWidget *layoutWidget;
    QVBoxLayout *verticalLayout_2;
    QCheckBox *Channel1_checkbox;
    QScrollBar *packetlost_bar1;
    QLabel *lostrate_label1;
    QCheckBox *channel2_checkbox;
    QScrollBar *packetlost_bar2;
    QLabel *lostrate_label2;
    QCheckBox *channel3_checkbox;
    QScrollBar *packetlost_bar3;
    QLabel *lostrate_label3;
    QDockWidget *tooldock;
    QWidget *dockWidgetContents;
    QWidget *layoutWidget1;
    QHBoxLayout *horizontalLayout;
    QPushButton *Play_btn;
    QPushButton *Stop_btn;
    QProgressBar *progressBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *Channel_simClass)
    {
        if (Channel_simClass->objectName().isEmpty())
            Channel_simClass->setObjectName("Channel_simClass");
        Channel_simClass->setEnabled(true);
        Channel_simClass->resize(600, 400);
        Channel_simClass->setMaximumSize(QSize(16777215, 400));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/Channel_sim/resouce/avic.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        Channel_simClass->setWindowIcon(icon);
        file_action = new QAction(Channel_simClass);
        file_action->setObjectName("file_action");
        QIcon icon1;
        icon1.addFile(QString::fromUtf8(":/Channel_sim/resouce/24gf-folderOpen-copy.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        file_action->setIcon(icon1);
        actionchannel = new QAction(Channel_simClass);
        actionchannel->setObjectName("actionchannel");
        actionlog = new QAction(Channel_simClass);
        actionlog->setObjectName("actionlog");
        actionvideo = new QAction(Channel_simClass);
        actionvideo->setObjectName("actionvideo");
        centralWidget = new QWidget(Channel_simClass);
        centralWidget->setObjectName("centralWidget");
        verticalLayout = new QVBoxLayout(centralWidget);
        verticalLayout->setSpacing(6);
        verticalLayout->setContentsMargins(11, 11, 11, 11);
        verticalLayout->setObjectName("verticalLayout");
        Channel_simClass->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(Channel_simClass);
        menuBar->setObjectName("menuBar");
        menuBar->setGeometry(QRect(0, 0, 600, 33));
        menu = new QMenu(menuBar);
        menu->setObjectName("menu");
        QIcon icon2;
        icon2.addFile(QString::fromUtf8(":/Channel_sim/resouce/file.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        menu->setIcon(icon2);
        view_action = new QMenu(menuBar);
        view_action->setObjectName("view_action");
        QIcon icon3;
        icon3.addFile(QString::fromUtf8(":/Channel_sim/resouce/\345\210\227\350\241\250\350\247\206\345\233\276.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        view_action->setIcon(icon3);
        Channel_simClass->setMenuBar(menuBar);
        mainToolBar = new QToolBar(Channel_simClass);
        mainToolBar->setObjectName("mainToolBar");
        Channel_simClass->addToolBar(Qt::ToolBarArea::TopToolBarArea, mainToolBar);
        dockWidget_4 = new QDockWidget(Channel_simClass);
        dockWidget_4->setObjectName("dockWidget_4");
        dockWidget_4->setMinimumSize(QSize(136, 274));
        dockWidget_4->setMaximumSize(QSize(139, 524287));
        dockWidgetContents_4 = new QWidget();
        dockWidgetContents_4->setObjectName("dockWidgetContents_4");
        listWidget = new QListWidget(dockWidgetContents_4);
        listWidget->setObjectName("listWidget");
        listWidget->setGeometry(QRect(0, 0, 131, 321));
        layoutWidget = new QWidget(dockWidgetContents_4);
        layoutWidget->setObjectName("layoutWidget");
        layoutWidget->setGeometry(QRect(20, 10, 101, 241));
        verticalLayout_2 = new QVBoxLayout(layoutWidget);
        verticalLayout_2->setSpacing(6);
        verticalLayout_2->setContentsMargins(11, 11, 11, 11);
        verticalLayout_2->setObjectName("verticalLayout_2");
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        Channel1_checkbox = new QCheckBox(layoutWidget);
        Channel1_checkbox->setObjectName("Channel1_checkbox");

        verticalLayout_2->addWidget(Channel1_checkbox);

        packetlost_bar1 = new QScrollBar(layoutWidget);
        packetlost_bar1->setObjectName("packetlost_bar1");
        packetlost_bar1->setOrientation(Qt::Orientation::Horizontal);

        verticalLayout_2->addWidget(packetlost_bar1);

        lostrate_label1 = new QLabel(layoutWidget);
        lostrate_label1->setObjectName("lostrate_label1");

        verticalLayout_2->addWidget(lostrate_label1);

        channel2_checkbox = new QCheckBox(layoutWidget);
        channel2_checkbox->setObjectName("channel2_checkbox");

        verticalLayout_2->addWidget(channel2_checkbox);

        packetlost_bar2 = new QScrollBar(layoutWidget);
        packetlost_bar2->setObjectName("packetlost_bar2");
        packetlost_bar2->setOrientation(Qt::Orientation::Horizontal);

        verticalLayout_2->addWidget(packetlost_bar2);

        lostrate_label2 = new QLabel(layoutWidget);
        lostrate_label2->setObjectName("lostrate_label2");

        verticalLayout_2->addWidget(lostrate_label2);

        channel3_checkbox = new QCheckBox(layoutWidget);
        channel3_checkbox->setObjectName("channel3_checkbox");

        verticalLayout_2->addWidget(channel3_checkbox);

        packetlost_bar3 = new QScrollBar(layoutWidget);
        packetlost_bar3->setObjectName("packetlost_bar3");
        packetlost_bar3->setOrientation(Qt::Orientation::Horizontal);

        verticalLayout_2->addWidget(packetlost_bar3);

        lostrate_label3 = new QLabel(layoutWidget);
        lostrate_label3->setObjectName("lostrate_label3");
        lostrate_label3->setMidLineWidth(-1);

        verticalLayout_2->addWidget(lostrate_label3);

        dockWidget_4->setWidget(dockWidgetContents_4);
        Channel_simClass->addDockWidget(Qt::DockWidgetArea::LeftDockWidgetArea, dockWidget_4);
        tooldock = new QDockWidget(Channel_simClass);
        tooldock->setObjectName("tooldock");
        tooldock->setEnabled(true);
        tooldock->setMinimumSize(QSize(600, 57));
        tooldock->setMaximumSize(QSize(600, 57));
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName("dockWidgetContents");
        layoutWidget1 = new QWidget(dockWidgetContents);
        layoutWidget1->setObjectName("layoutWidget1");
        layoutWidget1->setGeometry(QRect(20, 0, 64, 26));
        horizontalLayout = new QHBoxLayout(layoutWidget1);
        horizontalLayout->setSpacing(6);
        horizontalLayout->setContentsMargins(11, 11, 11, 11);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        Play_btn = new QPushButton(layoutWidget1);
        Play_btn->setObjectName("Play_btn");
        Play_btn->setMouseTracking(false);
        Play_btn->setStyleSheet(QString::fromUtf8("QPushButton:pressed {\n"
"    /* \351\274\240\346\240\207\346\214\211\344\270\213\347\212\266\346\200\201 */\n"
"    background-color: #999;\n"
"}"));
        QIcon icon4;
        icon4.addFile(QString::fromUtf8(":/Channel_sim/resouce/\345\274\200\345\247\2131.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        Play_btn->setIcon(icon4);

        horizontalLayout->addWidget(Play_btn);

        Stop_btn = new QPushButton(layoutWidget1);
        Stop_btn->setObjectName("Stop_btn");
        Stop_btn->setStyleSheet(QString::fromUtf8("QPushButton:pressed {\n"
"    /* \351\274\240\346\240\207\346\214\211\344\270\213\347\212\266\346\200\201 */\n"
"    background-color: #999;\n"
"}"));
        QIcon icon5;
        icon5.addFile(QString::fromUtf8(":/Channel_sim/resouce/24gf-stop.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        Stop_btn->setIcon(icon5);

        horizontalLayout->addWidget(Stop_btn);

        progressBar = new QProgressBar(dockWidgetContents);
        progressBar->setObjectName("progressBar");
        progressBar->setGeometry(QRect(340, 0, 251, 23));
        progressBar->setValue(24);
        tooldock->setWidget(dockWidgetContents);
        Channel_simClass->addDockWidget(Qt::DockWidgetArea::BottomDockWidgetArea, tooldock);
        statusBar = new QStatusBar(Channel_simClass);
        statusBar->setObjectName("statusBar");
        Channel_simClass->setStatusBar(statusBar);

        menuBar->addAction(menu->menuAction());
        menuBar->addAction(view_action->menuAction());
        menu->addAction(file_action);
        view_action->addAction(actionchannel);
        view_action->addAction(actionlog);
        view_action->addAction(actionvideo);

        retranslateUi(Channel_simClass);
        QObject::connect(actionchannel, &QAction::triggered, dockWidget_4, qOverload<>(&QDockWidget::show));
        QObject::connect(actionlog, &QAction::triggered, tooldock, qOverload<>(&QDockWidget::show));

        QMetaObject::connectSlotsByName(Channel_simClass);
    } // setupUi

    void retranslateUi(QMainWindow *Channel_simClass)
    {
        Channel_simClass->setWindowTitle(QCoreApplication::translate("Channel_simClass", "\346\250\241\346\213\237\346\227\240\347\272\277\345\244\232\344\275\223\345\210\266\345\217\221\351\200\201\347\253\257", nullptr));
        file_action->setText(QCoreApplication::translate("Channel_simClass", "open", nullptr));
        actionchannel->setText(QCoreApplication::translate("Channel_simClass", "channel", nullptr));
        actionlog->setText(QCoreApplication::translate("Channel_simClass", "tool", nullptr));
        actionvideo->setText(QCoreApplication::translate("Channel_simClass", "video", nullptr));
        menu->setTitle(QCoreApplication::translate("Channel_simClass", "\346\226\207\344\273\266", nullptr));
        view_action->setTitle(QCoreApplication::translate("Channel_simClass", "\350\247\206\345\233\276", nullptr));
        Channel1_checkbox->setText(QCoreApplication::translate("Channel_simClass", "\344\277\241\351\201\2231", nullptr));
        lostrate_label1->setText(QCoreApplication::translate("Channel_simClass", "Drop\357\274\232", nullptr));
        channel2_checkbox->setText(QCoreApplication::translate("Channel_simClass", "\344\277\241\351\201\2232", nullptr));
        lostrate_label2->setText(QCoreApplication::translate("Channel_simClass", "Drop\357\274\232", nullptr));
        channel3_checkbox->setText(QCoreApplication::translate("Channel_simClass", "\344\277\241\351\201\2233", nullptr));
        lostrate_label3->setText(QCoreApplication::translate("Channel_simClass", "Drop\357\274\232", nullptr));
        Play_btn->setText(QString());
        Stop_btn->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class Channel_simClass: public Ui_Channel_simClass {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CHANNEL_SIM_H
