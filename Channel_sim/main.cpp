#include "Channel_sim.h"
#include <QtWidgets/QApplication>
#include "logemitter.h"   

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);
    previousMessageHandler = qInstallMessageHandler(customMessageHandler);

    Channel_sim w;
    w.show();
    return a.exec();
}
