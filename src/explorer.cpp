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
  QCoreApplication::setApplicationVersion("1.1");
  QCoreApplication::setApplicationName("Data Explorer");
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "The file to open.");
  parser.process(app);
  const QStringList args = parser.positionalArguments();

  MainWindow window;
  if(args.size())
  {
    QString file_name = args.at(0);
    window.read_file(file_name);
  }
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
    ItemData *item_data_prn, ncvar_t *ncvar, grid_policy_t *grid_policy) :
    m_file_name(file_name),
    m_grp_nm_fll(grp_nm_fll),
    m_item_nm(item_nm),
    m_kind(kind),
    m_item_data_prn(item_data_prn),
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
  ItemData *m_item_data_prn; //  (Variable/Group) item data of the parent group (to get list of variables in group)
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
  m_tree->set_main_window(this);
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
  //about
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_about = new QAction(tr("&About"), this);
  m_action_about->setStatusTip(tr("Show the application's About box"));
  connect(m_action_about, SIGNAL(triggered()), this, SLOT(about()));

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
  m_menu_help = menuBar()->addMenu(tr("&Help"));
  m_menu_help->addAction(m_action_about);

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
//MainWindow::about
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::about()
{
  QMessageBox::about(this,
    tr("About Data Explorer"),
    tr("(c) 2015-2016 Pedro Vicente -- Space Research Software LLC\n\n"));
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
    (ItemData*)NULL,
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

  //get item data (of parent item), to store a list of variable names 
  ItemData *item_data_prn = get_item_data(tree_item_parent);
  assert(item_data_prn->m_kind == ItemData::Group || item_data_prn->m_kind == ItemData::Root);

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

    //store variable name in parent group item (for coordinate variables detection)
    item_data_prn->m_var_nms.push_back(var_nm);

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
      item_data_prn,
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
      item_data_prn,
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
//MainWindow::add_table
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_table(ItemData *item_data)
{
  ChildWindow *window = new ChildWindow(this, item_data);
  m_mdi_area->addSubWindow(window);
  window->show();
}

///////////////////////////////////////////////////////////////////////////////////////
//TableWidget
///////////////////////////////////////////////////////////////////////////////////////

class TableWidget : public QTableWidget
{
public:
  TableWidget(QWidget *parent, ItemData *item_data);
  void previous_layer(int idx_layer);
  void next_layer(int idx_layer);

  std::vector<int> m_layer;  // current selected layer of a dimension > 2 
  ItemData *m_item_data; // the tree item that generated this grid (convenience pointer to data in ItemData)
  ncvar_t *m_ncvar; // netCDF variable to display (convenience pointer to data in ItemData)
  void show_grid();
  const char* get_format(const nc_type typ);

protected:
  int m_nbr_rows;   // number of rows
  int m_nbr_cols;   // number of columns
  int m_dim_rows;   // choose rows (convenience duplicate to data in ItemData)
  int m_dim_cols;   // choose columns (convenience duplicate to data in ItemData)
  std::vector<ncvar_t *> m_ncvar_crd; // optional coordinate variables for variable (convenience duplicate to data in ItemData)
};

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::ChildWindow
///////////////////////////////////////////////////////////////////////////////////////

ChildWindow::ChildWindow(QWidget *parent, ItemData *item_data) :
QMainWindow(parent)
{
  float *buf_float = NULL;
  double *buf_double = NULL;
  int *buf_int = NULL;
  short *buf_short = NULL;
  signed char *buf_byte = NULL;
  unsigned char *buf_ubyte = NULL;
  unsigned short *buf_ushort = NULL;
  unsigned int *buf_uint = NULL;
  long long *buf_int64 = NULL;
  unsigned long long *buf_uint64 = NULL;

  m_table = new TableWidget(parent, item_data);
  setCentralWidget(m_table);

  QSignalMapper *signal_mapper_next;
  QSignalMapper *signal_mapper_previous;
  QSignalMapper *signal_mapper_combo;
  //data has layers
  if(item_data->m_ncvar->m_ncdim.size() > 2)
  {
    m_tool_bar = addToolBar(tr("Layers"));
    signal_mapper_next = new QSignalMapper(this);
    signal_mapper_previous = new QSignalMapper(this);
    signal_mapper_combo = new QSignalMapper(this);
    connect(signal_mapper_next, SIGNAL(mapped(int)), this, SLOT(next_layer(int)));
    connect(signal_mapper_previous, SIGNAL(mapped(int)), this, SLOT(previous_layer(int)));
    connect(signal_mapper_combo, SIGNAL(mapped(int)), this, SLOT(combo_layer(int)));
  }

  //number of dimensions above a two-dimensional dataset
  for(size_t idx_dmn = 0; idx_dmn < m_table->m_layer.size(); idx_dmn++)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //next layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_next  = new QAction(tr("&Next layer..."), this);
    action_next->setIcon(QIcon(":/images/right.png"));
    action_next->setStatusTip(tr("Next layer"));
    connect(action_next, SIGNAL(triggered()), signal_mapper_next, SLOT(map()));
    signal_mapper_next->setMapping(action_next, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //previous layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_previous = new QAction(tr("&Previous layer..."), this);
    action_previous->setIcon(QIcon(":/images/left.png"));
    action_previous->setStatusTip(tr("Previous layer"));
    connect(action_previous, SIGNAL(triggered()), signal_mapper_previous, SLOT(map()));
    signal_mapper_previous->setMapping(action_previous, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add to toolbar
    ///////////////////////////////////////////////////////////////////////////////////////

    m_tool_bar->addAction(action_next);
    m_tool_bar->addAction(action_previous);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add combo box with layers, fill with possible coordinate variables and store combo in vector 
    ///////////////////////////////////////////////////////////////////////////////////////

    QComboBox *combo = new QComboBox;
    QFont font = combo->font();
    font.setPointSize(9);
    combo->setFont(font);
    QStringList list;

    //coordinate variable exists
    if(item_data->m_ncvar_crd[idx_dmn] != NULL)
    {
      void *buf = item_data->m_ncvar_crd[idx_dmn]->m_buf;
      size_t size = item_data->m_ncvar->m_ncdim[idx_dmn].m_size;
      QString str;
      switch(item_data->m_ncvar_crd[idx_dmn]->m_nc_type)
      {
      case NC_FLOAT:
        buf_float = static_cast<float*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_FLOAT), buf_float[idx]);
          list.append(str);
        }
        break;
      case NC_DOUBLE:
        buf_double = static_cast<double*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_DOUBLE), buf_double[idx]);
        }
        break;
      case NC_INT:
        buf_int = static_cast<int*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_INT), buf_int[idx]);
        }
        break;
      case NC_SHORT:
        buf_short = static_cast<short*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_SHORT), buf_short[idx]);
        }
        break;
      case NC_BYTE:
        buf_byte = static_cast<signed char*>  (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_BYTE), buf_byte[idx]);
        }
        break;
      case NC_UBYTE:
        buf_ubyte = static_cast<unsigned char*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_UBYTE), buf_ubyte[idx]);
        }
        break;
      case NC_USHORT:
        buf_ushort = static_cast<unsigned short*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_USHORT), buf_ushort[idx]);
        }
        break;
      case NC_UINT:
        buf_uint = static_cast<unsigned int*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_UINT), buf_uint[idx]);
        }
        break;
      case NC_INT64:
        buf_int64 = static_cast<long long*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_INT64), buf_int64[idx]);
        }
        break;
      case NC_UINT64:
        buf_uint64 = static_cast<unsigned long long*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(m_table->get_format(NC_UINT64), buf_uint64[idx]);
        }
        break;
      } //switch
    }
    else
    {
      for(unsigned int idx = 0; idx < item_data->m_ncvar->m_ncdim[idx_dmn].m_size; idx++)
      {
        QString str;
        str.sprintf("%u", idx + 1);
        list.append(str);
      }
    }

    combo->addItems(list);
    connect(combo, SIGNAL(currentIndexChanged(int)), signal_mapper_combo, SLOT(map()));
    signal_mapper_combo->setMapping(combo, idx_dmn);
    m_tool_bar->addWidget(combo);
    m_vec_combo.push_back(combo);
  }

}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::previous_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::previous_layer(int idx_layer)
{
  m_table->previous_layer(idx_layer);
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_table->m_layer[idx_layer]);
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::next_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::next_layer(int idx_layer)
{
  m_table->next_layer(idx_layer);
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_table->m_layer[idx_layer]);
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::combo_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::combo_layer(int idx_layer)
{
  QComboBox *combo = m_vec_combo.at(idx_layer);
  m_table->m_layer[idx_layer] = combo->currentIndex();;
  m_table->show_grid();
  m_table->update();
}

///////////////////////////////////////////////////////////////////////////////////////
//TableWidget::TableWidget
///////////////////////////////////////////////////////////////////////////////////////

TableWidget::TableWidget(QWidget *parent, ItemData *item_data) :
QTableWidget(parent),
m_item_data(item_data),
m_ncvar(item_data->m_ncvar),
m_dim_rows(item_data->m_grid_policy->m_dim_rows),
m_dim_cols(item_data->m_grid_policy->m_dim_cols),
m_ncvar_crd(item_data->m_ncvar_crd)
{
  setWindowTitle(QString::fromStdString(item_data->m_item_nm));

  //currently selected layers for dimensions greater than two are the first layer
  if(m_ncvar->m_ncdim.size() > 2)
  {
    for(size_t idx_dmn = 0; idx_dmn < m_ncvar->m_ncdim.size() - 2; idx_dmn++)
    {
      m_layer.push_back(0);
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //define grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(m_ncvar->m_ncdim.size() == 0)
  {
    m_nbr_rows = 1;
    m_nbr_cols = 1;
  }
  else if(m_ncvar->m_ncdim.size() == 1)
  {
    assert(m_dim_rows == 0);
    m_nbr_rows = m_ncvar->m_ncdim[m_dim_rows].m_size;
    m_nbr_cols = 1;
  }
  else
  {
    m_nbr_rows = m_ncvar->m_ncdim[m_dim_rows].m_size;
    m_nbr_cols = m_ncvar->m_ncdim[m_dim_cols].m_size;
  }
  setRowCount(m_nbr_rows);
  setColumnCount(m_nbr_cols);

  //show data
  this->show_grid();
}


///////////////////////////////////////////////////////////////////////////////////////
//TableWidget::previous_layer
///////////////////////////////////////////////////////////////////////////////////////

void TableWidget::previous_layer(int idx_layer)
{
  m_layer[idx_layer]--;
  if(m_layer[idx_layer] < 0)
  {
    m_layer[idx_layer] = 0;
    return;
  }
  show_grid();
  update();
}

///////////////////////////////////////////////////////////////////////////////////////
//TableWidget::next_layer
///////////////////////////////////////////////////////////////////////////////////////

void TableWidget::next_layer(int idx_layer)
{
  m_layer[idx_layer]++;
  if((size_t)m_layer[idx_layer] >= m_ncvar->m_ncdim[idx_layer].m_size)
  {
    m_layer[idx_layer] = m_ncvar->m_ncdim[idx_layer].m_size - 1;
    return;
  }
  show_grid();
  update();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableWidget::get_format
//Provide sprintf() format string for specified netCDF type
//Based on NCO utilities
/////////////////////////////////////////////////////////////////////////////////////////////////////

const char* TableWidget::get_format(const nc_type typ)
{
  switch(typ)
  {
  case NC_FLOAT:
    return "%g";
  case NC_DOUBLE:
    return "%.12g";
  case NC_INT:
    return "%i";
  case NC_SHORT:
    return "%hi";
  case NC_CHAR:
    return "%c";
  case NC_BYTE:
    return "%hhi";
  case NC_UBYTE:
    return "%hhu";
  case NC_USHORT:
    return "%hu";
  case NC_UINT:
    return "%u";
  case NC_INT64:
    return "%lli";
  case NC_UINT64:
    return "%llu";
  case NC_STRING:
    return "%s";
  }
  return NULL;
}


///////////////////////////////////////////////////////////////////////////////////////
//TableWidget::show_grid
///////////////////////////////////////////////////////////////////////////////////////

void TableWidget::show_grid()
{
  size_t idx_buf = 0;
  //3D
  if(m_layer.size() == 1)
  {
    idx_buf = m_layer[0] * m_nbr_rows * m_nbr_cols;
  }
  //4D
  else if(m_layer.size() == 2)
  {
    idx_buf = m_layer[0] * m_ncvar->m_ncdim[1].m_size + m_layer[1];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  //5D
  else if(m_layer.size() == 3)
  {
    idx_buf = m_layer[0] * m_ncvar->m_ncdim[1].m_size * m_ncvar->m_ncdim[2].m_size
      + m_layer[1] * m_ncvar->m_ncdim[2].m_size
      + m_layer[2];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  float *buf_float = NULL;
  double *buf_double = NULL;
  int *buf_int = NULL;
  short *buf_short = NULL;
  char *buf_char = NULL;
  signed char *buf_byte = NULL;
  unsigned char *buf_ubyte = NULL;
  unsigned short *buf_ushort = NULL;
  unsigned int *buf_uint = NULL;
  long long *buf_int64 = NULL;
  unsigned long long *buf_uint64 = NULL;
  char* *buf_string = NULL;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  switch(m_ncvar->m_nc_type)
  {
  case NC_FLOAT:
    buf_float = static_cast<float*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_FLOAT), buf_float[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_DOUBLE:
    buf_double = static_cast<double*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_DOUBLE), buf_double[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_INT:
    buf_int = static_cast<int*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_INT), buf_int[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_SHORT:
    buf_short = static_cast<short*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_SHORT), buf_short[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_CHAR:
    buf_char = static_cast<char*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_CHAR), buf_char[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_BYTE:
    buf_byte = static_cast<signed char*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_BYTE), buf_byte[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_UBYTE:
    buf_ubyte = static_cast<unsigned char*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_UBYTE), buf_ubyte[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_USHORT:
    buf_ushort = static_cast<unsigned short*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_USHORT), buf_ushort[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_UINT:
    buf_uint = static_cast<unsigned int*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_UINT), buf_uint[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_INT64:
    buf_int64 = static_cast<long long*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_INT64), buf_int64[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_UINT64:
    buf_uint64 = static_cast<unsigned long long*> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_UINT64), buf_uint64[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  case NC_STRING:
    buf_string = static_cast<char**> (m_ncvar->m_buf);
    for(int idx_row = 0; idx_row < m_nbr_rows; idx_row++)
    {
      for(int idx_col = 0; idx_col < m_nbr_cols; idx_col++)
      {
        QString str;
        str.sprintf(get_format(NC_STRING), buf_string[idx_buf]);
        QTableWidgetItem *item = new QTableWidgetItem(str);
        this->setItem(idx_row, idx_col, item);
        idx_buf++;
      }
    }
    break;
  }//switch

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
  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Variable);
  m_main_window->add_table(item_data);
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
    int has_crd_var = 0;

    //dimensions belong to groups
    if(nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
    {

    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //look up possible coordinate variables
    //traverse all variables in group and match a variable name 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    for(size_t idx_var = 0; idx_var < item_data->m_item_data_prn->m_var_nms.size(); idx_var++)
    {
      std::string var_nm(item_data->m_item_data_prn->m_var_nms[idx_var]);

      if(var_nm == std::string(dmn_nm_var))
      {
        has_crd_var = 1;
        break;
      }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //a coordinate variable was found
    //since the lookup was only in the same group, get the variable information on this group 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    if(has_crd_var)
    {
      int crd_var_id;
      char crd_var_nm[NC_MAX_NAME + 1];
      int crd_nbr_dmn;
      int crd_var_dimid[NC_MAX_VAR_DIMS];
      size_t crd_dmn_sz[NC_MAX_VAR_DIMS];
      nc_type crd_var_type = NC_NAT;

      // get coordinate variable ID (using the dimension name, since there was a match to a variable)
      if(nc_inq_varid(grp_id, dmn_nm_var, &crd_var_id) != NC_NOERR)
      {

      }

      if(nc_inq_var(grp_id, crd_var_id, crd_var_nm, &crd_var_type, &crd_nbr_dmn, crd_var_dimid, (int *)NULL) != NC_NOERR)
      {

      }

      assert(std::string(crd_var_nm) == std::string(dmn_nm_var));

      if(crd_nbr_dmn == 1)
      {
        //get size
        if(nc_inq_dim(grp_id, crd_var_dimid[0], (char *)NULL, &crd_dmn_sz[0]) != NC_NOERR)
        {

        }

        //store dimension 
        std::vector<ncdim_t> ncdim; //dimensions for each variable 
        ncdim_t dim(dmn_nm_var, crd_dmn_sz[0]);
        ncdim.push_back(dim);

        //store a ncvar_t
        ncvar_t *ncvar = new ncvar_t(crd_var_nm, crd_var_type, ncdim);

        //allocate, load 
        ncvar->store(load_variable(grp_id, crd_var_id, crd_var_type, crd_dmn_sz[0]));

        //and store in tree 
        item_data->m_ncvar_crd.push_back(ncvar);
      }
    }
    else
    {
      item_data->m_ncvar_crd.push_back(NULL); //no coordinate variable for this dimension
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

