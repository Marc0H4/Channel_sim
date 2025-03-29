#include "Channel_sim.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);
    Channel_sim w;
    w.show();
    return a.exec();
}
