#pragma once

#include <QMainWindow>
#include <QGridLayout>
#include <QSplitter>
#include <QTreeView>
#include "databasemanager.h"
#include "ContentManager.h"
#include "sorter.h"
#include "manipulator.h"
#include <vsgQt/ViewerWindow.h>
#include <QUndoView>
#include <QUndoStack>
#include <QRegularExpression>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void addToRoot(vsg::ref_ptr<vsg::Node> node);
//    void setTilesModel(SceneModel *model);
    void openRoute();
    void search();
    void addObject();
    void pushCommand(QUndoCommand *command);

private:
    QWindow* initilizeVSGwindow();

    void constructWidgets();

    Ui::MainWindow *ui;
    ContentManager *content;
    vsg::ref_ptr<vsg::Group> scene;
    vsg::ref_ptr<vsg::Options> options;
    vsg::ref_ptr<vsg::Builder> builder;
    double horizonMountainHeight;
    vsgQt::ViewerWindow *viewerWindow;
    QScopedPointer<DatabaseManager> database;
    vsg::ref_ptr<Manipulator> manipulator;

    QUndoStack *undoStack;
    QUndoView *undoView;

};
