//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file 
//OPeNDAP
//http://www.esrl.noaa.gov/psd/thredds/dodsC/Datasets/cmap/enh/precip.mon.mean.nc

#include <QApplication>
#include <QMetaType>
#include <cassert>
#include <vector>
#include <algorithm>
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
//ItemData
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ItemData
{
public:
  enum ItemKind
  {
    Root,
    Group,
    Variable,
    Attribute
  };

  ItemData(ItemKind kind, const std::string& file_name, const std::string& grp_nm_fll, const std::string& item_nm,
    ncvar_t *ncvar, grid_policy_t *grid_policy) :
    m_file_name(file_name),
    m_grp_nm_fll(grp_nm_fll),
    m_item_nm(item_nm),
    m_kind(kind),
    m_ncvar(ncvar),
    m_grid_policy(grid_policy)
  {
  }
  ~ItemData()
  {
    delete m_ncvar;
    for(size_t idx_dmn = 0; idx_dmn < m_ncvar_crd.size(); idx_dmn++)
    {
      delete m_ncvar_crd[idx_dmn];
    }
    delete m_grid_policy;
  }
  std::string m_file_name;  // (Root/Variable/Group/Attribute) file name
  std::string m_grp_nm_fll; // (Group) full name of group
  std::string m_item_nm; // (Root/Variable/Group/Attribute ) item name to display on tree
  ItemKind m_kind; // (Root/Variable/Group/Attribute) type of item 
  std::vector<std::string> m_var_nms; // (Group) list of variables if item is group (filled in file iteration)
  ncvar_t *m_ncvar; // (Variable) netCDF variable to display
  std::vector<ncvar_t *> m_ncvar_crd; // (Variable) optional coordinate variables for variable
  grid_policy_t *m_grid_policy; // (Variable) current grid policy (interactive)
};

Q_DECLARE_METATYPE(ItemData*);

/////////////////////////////////////////////////////////////////////////////////////////////////////
//get_item_data
/////////////////////////////////////////////////////////////////////////////////////////////////////

ItemData* get_item_data(QTreeWidgetItem *item)
{
  QVariant data = item->data(0, Qt::UserRole);
  ItemData *item_data = data.value<ItemData*>();
  return item_data;
}

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

  m_tree = new FileTreeWidget();
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
  m_icon_dataset = QIcon(":/images/document.png");

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

  //group item
  ItemData *item_data_grp = new ItemData(ItemData::Group,
    str_file_name,
    "/",
    "/",
    (ncvar_t*)NULL,
    (grid_policy_t*)NULL);

  //add root
  QTreeWidgetItem *root_item = new QTreeWidgetItem(m_tree);
  index = file_name.lastIndexOf(QChar('/'));
  len = file_name.length();
  name = file_name.right(len - index - 1);
  root_item->setText(0, name);
  root_item->setIcon(0, m_icon_group);
  QVariant data;
  data.setValue(item_data_grp);
  root_item->setData(0, Qt::UserRole, data);

  if(iterate(str_file_name, nc_id, root_item) != NC_NOERR)
  {

  }

  if(nc_close(nc_id) != NC_NOERR)
  {

  }


  return NC_NOERR;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::iterate
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::iterate(const std::string& file_name, const int grp_id, QTreeWidgetItem *tree_item_parent)
{
  char grp_nm[NC_MAX_NAME + 1]; // group name 
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char *grp_nm_fll = NULL; // group full name 
  int nbr_att; // number of attributes 
  int nbr_dmn_grp; // number of dimensions for group 
  int nbr_var; // number of variables 
  int nbr_grp; // number of sub-groups in this group 
  int nbr_dmn_var; // number of dimensions for variable 
  nc_type var_typ; // netCDF type 
  int *grp_ids; // sub-group IDs array
  size_t grp_nm_lng; //lenght of full group name
  int var_dimid[NC_MAX_VAR_DIMS]; // dimensions for variable
  size_t dmn_sz[NC_MAX_VAR_DIMS]; // dimensions for variable sizes
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name

  // get full name of (parent) group
  if(nc_inq_grpname_full(grp_id, &grp_nm_lng, NULL) != NC_NOERR)
  {

  }

  grp_nm_fll = new char[grp_nm_lng + 1];

  if(nc_inq_grpname_full(grp_id, &grp_nm_lng, grp_nm_fll) != NC_NOERR)
  {

  }

  if(nc_inq(grp_id, &nbr_dmn_grp, &nbr_var, &nbr_att, (int *)NULL) != NC_NOERR)
  {

  }

  for(int idx_var = 0; idx_var < nbr_var; idx_var++)
  {
    std::vector<ncdim_t> ncdim; //dimensions for each variable 

    if(nc_inq_var(grp_id, idx_var, var_nm, &var_typ, &nbr_dmn_var, var_dimid, &nbr_att) != NC_NOERR)
    {

    }

    //get dimensions
    for(int idx_dmn = 0; idx_dmn < nbr_dmn_var; idx_dmn++)
    {
      //dimensions belong to groups
      if(nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
      {

      }

      //store dimension 
      ncdim_t dim(dmn_nm_var, dmn_sz[idx_dmn]);
      ncdim.push_back(dim);
    }

    //store a ncvar_t
    ncvar_t *ncvar = new ncvar_t(var_nm, var_typ, ncdim);

    //define a grid dimensions policy
    grid_policy_t *grid_policy = new grid_policy_t(ncdim);

    //append item
    ItemData *item_data_var = new ItemData(ItemData::Variable,
      file_name,
      grp_nm_fll,
      var_nm,
      ncvar,
      grid_policy);

    //append item
    QTreeWidgetItem *item_var = new QTreeWidgetItem(tree_item_parent);
    item_var->setText(0, var_nm);
    item_var->setIcon(0, m_icon_dataset);
    QVariant data;
    data.setValue(item_data_var);
    item_var->setData(0, Qt::UserRole, data);

  }

  if(nc_inq_grps(grp_id, &nbr_grp, (int *)NULL) != NC_NOERR)
  {

  }

  grp_ids = new int[nbr_grp];

  if(nc_inq_grps(grp_id, &nbr_grp, grp_ids) != NC_NOERR)
  {

  }

  for(int idx_grp = 0; idx_grp < nbr_grp; idx_grp++)
  {
    if(nc_inq_grpname(grp_ids[idx_grp], grp_nm) != NC_NOERR)
    {

    }

    //group item
    ItemData *item_data_grp = new ItemData(ItemData::Group,
      file_name,
      grp_nm_fll,
      grp_nm,
      (ncvar_t*)NULL,
      (grid_policy_t*)NULL);

    //group item
    QTreeWidgetItem *item_grp = new QTreeWidgetItem(tree_item_parent);
    item_grp->setText(0, grp_nm);
    item_grp->setIcon(0, m_icon_group);
    QVariant data;
    data.setValue(item_data_grp);
    item_grp->setData(0, Qt::UserRole, data);

    if(iterate(file_name, grp_ids[idx_grp], item_grp) != NC_NOERR)
    {

    }
  }

  delete[] grp_ids;
  delete[] grp_nm_fll;

  return NC_NOERR;
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::FileTreeWidget 
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::FileTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
  setContextMenuPolicy(Qt::CustomContextMenu);

  //right click menu
  connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(show_context_menu(const QPoint &)));

  //double click
  connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(grid()));
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::~FileTreeWidget
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::~FileTreeWidget()
{
  QTreeWidgetItemIterator it(this);
  while(*it)
  {
    ItemData *item_data = get_item_data(*it);
    delete item_data;
    ++it;
  }

}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::show_context_menu
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::show_context_menu(const QPoint &p)
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (itemAt(p));
  ItemData *item_data = get_item_data(item);
  if(item_data->m_kind != ItemData::Variable)
    return;
  QMenu menu;
  QAction *action_grid = new QAction("Grid...", this);;
  connect(action_grid, SIGNAL(triggered()), this, SLOT(grid()));
  menu.addAction(action_grid);
  menu.addSeparator();
  menu.exec(QCursor::pos());
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::grid
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::grid()
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (currentItem());
  this->load_item(item);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_item
/////////////////////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::load_item(QTreeWidgetItem  *item)
{
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name
  int nc_id;
  int grp_id;
  int var_id;
  nc_type var_type;
  int nbr_dmn;
  int var_dimid[NC_MAX_VAR_DIMS];
  size_t dmn_sz[NC_MAX_VAR_DIMS];
  size_t buf_sz; // variable size
  int fl_fmt;

  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Variable);

  //if not loaded, read buffer from file 
  if(item_data->m_ncvar->m_buf != NULL)
  {
    return;
  }

  if(nc_open(item_data->m_file_name.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR)
  {

  }

  //need a file format inquiry, since nc_inq_grp_full_ncid does not handle netCDF3 cases
  if(nc_inq_format(nc_id, &fl_fmt) != NC_NOERR)
  {

  }

  if(fl_fmt == NC_FORMAT_NETCDF4 || fl_fmt == NC_FORMAT_NETCDF4_CLASSIC)
  {
    // obtain group ID for netCDF4 files
    if(nc_inq_grp_full_ncid(nc_id, item_data->m_grp_nm_fll.c_str(), &grp_id) != NC_NOERR)
    {

    }
  }
  else
  {
    //make the group ID the file ID for netCDF3 cases
    grp_id = nc_id;
  }

  //all hunky dory from here 

  // get variable ID
  if(nc_inq_varid(grp_id, item_data->m_item_nm.c_str(), &var_id) != NC_NOERR)
  {

  }

  if(nc_inq_var(grp_id, var_id, var_nm, &var_type, &nbr_dmn, var_dimid, (int *)NULL) != NC_NOERR)
  {

  }

  //get dimensions 
  for(int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    //dimensions belong to groups
    if(nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
    {

    }
  }

  //define buffer size
  buf_sz = 1;
  for(int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    buf_sz *= dmn_sz[idx_dmn];
  }

  //allocate buffer and store in item data 
  item_data->m_ncvar->store(load_variable(grp_id, var_id, var_type, buf_sz));

  if(nc_close(nc_id) != NC_NOERR)
  {

  }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_variable
/////////////////////////////////////////////////////////////////////////////////////////////////////

void* FileTreeWidget::load_variable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz)
{
  void *buf = NULL;
  switch(var_type)
  {
  case NC_FLOAT:
    buf = malloc(buf_sz * sizeof(float));
    if(nc_get_var_float(nc_id, var_id, static_cast<float *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_DOUBLE:
    buf = malloc(buf_sz * sizeof(double));
    if(nc_get_var_double(nc_id, var_id, static_cast<double *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT:
    buf = malloc(buf_sz * sizeof(int));
    if(nc_get_var_int(nc_id, var_id, static_cast<int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_SHORT:
    buf = malloc(buf_sz * sizeof(short));
    if(nc_get_var_short(nc_id, var_id, static_cast<short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_CHAR:
    buf = malloc(buf_sz * sizeof(char));
    if(nc_get_var_text(nc_id, var_id, static_cast<char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_BYTE:
    buf = malloc(buf_sz * sizeof(signed char));
    if(nc_get_var_schar(nc_id, var_id, static_cast<signed char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UBYTE:
    buf = malloc(buf_sz * sizeof(unsigned char));
    if(nc_get_var_uchar(nc_id, var_id, static_cast<unsigned char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_USHORT:
    buf = malloc(buf_sz * sizeof(unsigned short));
    if(nc_get_var_ushort(nc_id, var_id, static_cast<unsigned short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT:
    buf = malloc(buf_sz * sizeof(unsigned int));
    if(nc_get_var_uint(nc_id, var_id, static_cast<unsigned int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT64:
    buf = malloc(buf_sz * sizeof(long long));
    if(nc_get_var_longlong(nc_id, var_id, static_cast<long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT64:
    buf = malloc(buf_sz * sizeof(unsigned long long));
    if(nc_get_var_ulonglong(nc_id, var_id, static_cast<unsigned long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_STRING:
    buf = malloc(buf_sz * sizeof(char*));
    if(nc_get_var_string(nc_id, var_id, static_cast<char* *>(buf)) != NC_NOERR)
    {
    }
    break;
  }
  return buf;
}

