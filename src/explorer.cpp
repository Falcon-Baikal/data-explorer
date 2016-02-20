//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file 
//OPeNDAP
//http://www.esrl.noaa.gov/psd/thredds/dodsC/Datasets/cmap/enh/precip.mon.mean.nc

#include <QApplication>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include "netcdf.h"
#include "explorer.hpp"

/////////////////////////////////////////////////////////////////////////////////////////////////////
//main
/////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
  Q_INIT_RESOURCE(explorer);
  QApplication app(argc, argv);
  MainWindow window;
  window.showMaximized();
  return app.exec();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ncdim_t
//a netCDF dimension has a name and a size
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ncdim_t
{
public:
  ncdim_t(const char* name, size_t size) :
    m_name(name),
    m_size(size)
  {
  }
  std::string m_name;
  size_t m_size;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ncvar_t
//a netCDF variable has a name, a netCDF type, data buffer, and an array of dimensions
//defined in iteration
//data buffer is stored on per load variable from tree using netCDF API from item input
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ncvar_t
{
public:
  ncvar_t(const char* name, nc_type nc_typ, const std::vector<ncdim_t> &ncdim) :
    m_name(name),
    m_nc_type(nc_typ),
    m_ncdim(ncdim)
  {
    m_buf = NULL;
  }
  ~ncvar_t()
  {
    switch(m_nc_type)
    {
    case NC_STRING:
      if(m_buf)
      {
        char **buf_string = NULL;
        size_t idx_buf = 0;
        buf_string = static_cast<char**> (m_buf);
        if(m_ncdim.size())
        {
          for(size_t idx_dmn = 0; idx_dmn < m_ncdim.size(); idx_dmn++)
          {
            for(size_t idx_sz = 0; idx_sz < m_ncdim[idx_dmn].m_size; idx_sz++)
            {
              free(buf_string[idx_buf]);
              idx_buf++;
            }
          }
        }
        else
        {
          free(*buf_string);
        }
        free(static_cast<char**>(buf_string));
      }
      break;
    default:
      free(m_buf);
    }
  }
  void store(void *buf)
  {
    m_buf = buf;
  }
  std::string m_name;
  nc_type m_nc_type;
  void *m_buf;
  std::vector<ncdim_t> m_ncdim;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//grid_policy_t
/////////////////////////////////////////////////////////////////////////////////////////////////////

class grid_policy_t
{
public:
  grid_policy_t(const std::vector<ncdim_t> &ncdim)
  {
    //define a grid policy
    if(ncdim.size() == 0)
    {
      m_dim_rows = -1;
      m_dim_cols = -1;
    }
    else if(ncdim.size() == 1)
    {
      m_dim_rows = 0;
      m_dim_cols = -1;
    }
    else if(ncdim.size() == 2)
    {
      m_dim_rows = 0;
      m_dim_cols = 1;
    }
    else if(ncdim.size() >= 3)
    {
      m_dim_cols = ncdim.size() - 1; //2 for 3D
      m_dim_rows = ncdim.size() - 2; //1 for 3D
      for(size_t idx_dmn = 0; idx_dmn < ncdim.size() - 2; idx_dmn++)
      {
        m_dim_layers.push_back(idx_dmn); //0 for 3D
      }
    }
  }
  int m_dim_rows;   // choose dimension to be displayed by rows 
  int m_dim_cols;   // choose dimension to be displayed by columns 
  std::vector<size_t> m_dim_layers; // choose dimensions to be displayed by layers 
};


/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::MainWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
  ///////////////////////////////////////////////////////////////////////////////////////
  //mdi area
  ///////////////////////////////////////////////////////////////////////////////////////

  m_mdi_area = new QMdiArea;
  setCentralWidget(m_mdi_area);

  setWindowTitle(tr("Data Explorer"));

  ///////////////////////////////////////////////////////////////////////////////////////
  //status bar
  ///////////////////////////////////////////////////////////////////////////////////////

  statusBar()->showMessage(tr("Ready"));

  ///////////////////////////////////////////////////////////////////////////////////////
  //dock for tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree_dock = new QDockWidget(this);
  m_tree_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

  ///////////////////////////////////////////////////////////////////////////////////////
  //browser tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree = new QTreeWidget;
  m_tree->setHeaderHidden(1);
  m_tree->setFixedWidth(300);
  //add dock
  m_tree_dock->setWidget(m_tree);
  addDockWidget(Qt::LeftDockWidgetArea, m_tree_dock);

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////////////////////////
  //open
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_open = new QAction(tr("&Open..."), this);
  m_action_open->setIcon(QIcon(":/images/open.png"));
  m_action_open->setShortcut(QKeySequence::Open);
  m_action_open->setStatusTip(tr("Open a file"));
  connect(m_action_open, SIGNAL(triggered()), this, SLOT(open()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //exit
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_exit = new QAction(tr("E&xit"), this);
  m_action_exit->setShortcut(tr("Ctrl+Q"));
  m_action_exit->setStatusTip(tr("Exit the application"));
  connect(m_action_exit, SIGNAL(triggered()), this, SLOT(close()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //recent files
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int i = 0; i < max_recent_files; ++i)
  {
    m_action_recent_file[i] = new QAction(this);
    m_action_recent_file[i]->setVisible(false);
    connect(m_action_recent_file[i], SIGNAL(triggered()), this, SLOT(open_recent_file()));
  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //menus
  ///////////////////////////////////////////////////////////////////////////////////////

  m_menu_file = menuBar()->addMenu(tr("&File"));
  m_menu_file->addAction(m_action_open);
  m_action_separator_recent = m_menu_file->addSeparator();
  for(int i = 0; i < max_recent_files; ++i)
    m_menu_file->addAction(m_action_recent_file[i]);
  m_menu_file->addSeparator();
  m_menu_file->addAction(m_action_exit);

  ///////////////////////////////////////////////////////////////////////////////////////
  //toolbar
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tool_bar = addToolBar(tr("&File"));
  m_tool_bar->addAction(m_action_open);

  //avoid popup on toolbar
  setContextMenuPolicy(Qt::NoContextMenu);

  ///////////////////////////////////////////////////////////////////////////////////////
  //settings
  ///////////////////////////////////////////////////////////////////////////////////////

  QSettings settings("space", "data_explorer");
  m_sl_recent_files = settings.value("recentFiles").toStringList();
  update_recent_file_actions();

  ///////////////////////////////////////////////////////////////////////////////////////
  //icons
  ///////////////////////////////////////////////////////////////////////////////////////

  m_icon_main = QIcon(":/images/sample.png");
  m_icon_group = QIcon(":/images/folder.png");

  ///////////////////////////////////////////////////////////////////////////////////////
  //set main window icon
  ///////////////////////////////////////////////////////////////////////////////////////

  setWindowIcon(m_icon_main);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::closeEvent
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::closeEvent(QCloseEvent *eve)
{
  QSettings settings("space", "data_explorer");
  settings.setValue("recentFiles", m_sl_recent_files);
  eve->accept();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::update_recent_file_actions
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::update_recent_file_actions()
{
  QMutableStringListIterator i(m_sl_recent_files);
  while(i.hasNext())
  {
    if(!QFile::exists(i.next()))
      i.remove();
  }

  for(int j = 0; j < max_recent_files; ++j)
  {
    if(j < m_sl_recent_files.count())
    {
      QString text = tr("&%1 %2")
        .arg(j + 1)
        .arg(stripped_name(m_sl_recent_files[j]));
      m_action_recent_file[j]->setText(text);
      m_action_recent_file[j]->setData(m_sl_recent_files[j]);
      m_action_recent_file[j]->setVisible(true);
    }
    else
    {
      m_action_recent_file[j]->setVisible(false);
    }
  }
  m_action_separator_recent->setVisible(!m_sl_recent_files.isEmpty());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::stripped_name
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString MainWindow::stripped_name(const QString &full_file_name)
{
  return QFileInfo(full_file_name).fileName();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::set_current_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::set_current_file(const QString &file_name)
{
  m_str_current_file = file_name;

  QString shownName = tr("Untitled");
  if(!m_str_current_file.isEmpty())
  {
    shownName = stripped_name(m_str_current_file);
    m_sl_recent_files.removeAll(m_str_current_file);
    m_sl_recent_files.prepend(m_str_current_file);
    update_recent_file_actions();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open()
{
  QString file_name = QFileDialog::getOpenFileName(this,
    tr("Open File"), ".",
    tr("netCDF Files (*.nc);;All files (*.*)"));

  if(file_name.isEmpty())
    return;

  if(this->read_file(file_name) == NC_NOERR)
  {
    this->set_current_file(file_name);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_recent_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_recent_file()
{
  QAction *action = qobject_cast<QAction *>(sender());
  if(action)
  {
    read_file(action->data().toString());
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::read_file
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::read_file(QString file_name)
{
  QByteArray ba;
  int nc_id;
  std::string path;
  std::string str_file_name;
  QTreeWidgetItem *tree_item;
  QString name;
  int index;
  int len;

  //convert QString to char*
  ba = file_name.toLatin1();

  //convert to std::string
  str_file_name = ba.data();

  if(nc_open(str_file_name.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR)
  {
    return NC2_ERR;
  }

  //add root
  tree_item = new QTreeWidgetItem(m_tree);
  index = file_name.lastIndexOf(QChar('/'));
  len = file_name.length();
  name = file_name.right(len - index - 1);
  tree_item->setText(0, name);
  tree_item->setIcon(0, m_icon_group);

  if(nc_close(nc_id) != NC_NOERR)
  {

  }


  return NC_NOERR;
}