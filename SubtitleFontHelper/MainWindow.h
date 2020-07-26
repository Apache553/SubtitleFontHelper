#ifndef __MAINWINDOW_H
#define __MAINWINDOW_H

#include <QMainWindow>

#include "FontDatabase.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow* ui;
private slots:

};
#endif 