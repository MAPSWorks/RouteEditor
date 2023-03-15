#include "StartDialog.h"
#include "ui_StartDialog.h"
#include <QSettings>
#include <vsg/io/read.h>
#include <vsgXchange/all.h>
#include "Constants.h"
#include "Register.h"

StartDialog::StartDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StartDialog)
{
    ui->setupUi(this);

    options = vsg::Options::create();
    options->fileCache = vsg::getEnv("RRS2_CACHE");
    options->paths = vsg::getEnvPaths("RRS2_ROOT");

    // add vsgXchange's support for reading and writing 3rd party file formats
    options->add(vsgXchange::all::create());

    QSettings settings(app::ORGANIZATION_NAME, app::APP_NAME);
    auto HMH = settings.value("HMH", 1.0).toDouble();
    auto NFR = settings.value("NFR", 0.0001).toDouble();

    ui->HMHSpin->setValue(HMH);
    ui->NFRSpin->setValue(NFR);
    ui->pointsSpinBox->setValue(settings.value("POINTSIZE", 3).toInt());
    ui->lodPointsSpinBox->setValue(settings.value("LOD_POINTS", 0.1).toDouble());
    ui->lodTilesSpinBox->setValue(settings.value("LOD_TILES", 0.5).toDouble());
    ui->cursorSpinBox->setValue(settings.value("CURSORSIZE", 3).toInt());

    routeModel = new QFileSystemModel(this);
    ui->routeTree->setModel(routeModel);
    ui->routeTree->setRootIndex(routeModel->setRootPath(qgetenv("RRS2_ROOT") + QDir::separator().toLatin1() + "routes"));
    auto skyfsmodel = new QFileSystemModel(this);
    ui->skyTree->setModel(skyfsmodel);
    ui->skyTree->setRootIndex(skyfsmodel->setRootPath(qgetenv("RRS2_ROOT") + QDir::separator().toLatin1() + "sky"));
    connect(ui->skyTree->selectionModel(), &QItemSelectionModel::selectionChanged, [this, skyfsmodel](const QItemSelection &selected, const QItemSelection &)
    {
        skyboxPath = skyfsmodel->filePath(selected.indexes().front());
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &StartDialog::load);
}
void StartDialog::updateSettings()
{
    QSettings settings(app::ORGANIZATION_NAME, app::APP_NAME);
    settings.setValue("HMH", ui->HMHSpin->value());
    settings.setValue("NFR", ui->NFRSpin->value());
    settings.setValue("COLORS", ui->comboBox->currentIndex());
    settings.setValue("POINTSIZE", ui->pointsSpinBox->value());
    settings.setValue("LOD_POINTS", ui->lodPointsSpinBox->value());
    settings.setValue("LOD_TILES", ui->lodTilesSpinBox->value());
    settings.setValue("CURSORSIZE", ui->cursorSpinBox->value());
}

void StartDialog::load()
{
    app::registerObjectFactoy();

    QFutureWatcher<vsg::ref_ptr<route::Tile>> loadWatcher;
    connect(&loadWatcher, &QFutureWatcher<vsg::ref_ptr<vsg::Node>>::progressValueChanged, ui->progressBar, &QProgressBar::setValue);
    connect(&loadWatcher, &QFutureWatcher<vsg::ref_ptr<vsg::Node>>::progressRangeChanged, ui->progressBar, &QProgressBar::setRange);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, &loadWatcher, &QFutureWatcher<vsg::ref_ptr<vsg::Node>>::cancel);

    QFutureWatcher<vsg::ref_ptr<DatabaseManager>> dbWatcher;
    connect(&dbWatcher, &QFutureWatcher<vsg::ref_ptr<DatabaseManager>>::finished, this, &StartDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, &dbWatcher, &QFutureWatcher<vsg::ref_ptr<DatabaseManager>>::cancel);

    auto selected = ui->routeTree->selectionModel()->selectedRows();

    auto load = [this](const QModelIndex &idx)
    {
        auto path = routeModel->filePath(idx);
        auto node = vsg::read_cast<route::Tile>(path.toStdString(), options);
        if(!node)
            throw DatabaseException(path);
        node->terrain->properties.dataVariance = vsg::DYNAMIC_DATA;
        node->texture->properties.dataVariance = vsg::DYNAMIC_DATA;
        node->setValue(app::PATH, path.toStdString());
        return node;
    };
    auto loadFuture = QtConcurrent::mapped(selected, load);
    database = loadFuture.then([fi=routeModel->fileInfo(selected.front()), options=options](QFuture<vsg::ref_ptr<route::Tile>> f)
    {
        auto databasePath = fi.absolutePath() + QDir::separator() + "database." + fi.suffix();
        auto database = vsg::read_cast<vsg::Group>(databasePath.toStdString(), options);
        if (!database)
            throw (DatabaseException(databasePath));
        database->setValue(app::PATH, databasePath.toStdString());
        auto group = route::SceneGroup::create();
        for (const auto &ptr : f) {
            group->addChild(ptr);
        }
        return DatabaseManager::create(database, group, options);
    });

    loadWatcher.setFuture(loadFuture);
    dbWatcher.setFuture(database);
}

StartDialog::~StartDialog()
{   
    delete ui;
}

