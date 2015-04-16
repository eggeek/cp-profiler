#include "pixelview.hh"
#include <chrono>
#include <cmath>

using namespace std::chrono;

/// ******* PIXEL_TREE_DIALOG ********

PixelTreeDialog::PixelTreeDialog(TreeCanvas* tc)
  : QDialog(tc)
{

  this->resize(600, 400);
  setLayout(&layout);
  layout.addWidget(&scrollArea);
  layout.addLayout(&controlLayout);

  controlLayout.addWidget(&scaleDown);
  controlLayout.addWidget(&scaleUp);

  scaleUp.setText("+");
  scaleDown.setText("-");

  QLabel* compLabel = new QLabel("compression");
  compLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  controlLayout.addWidget(compLabel);

  controlLayout.addWidget(&compressionSB);
  compressionSB.setMinimum(1);
  compressionSB.setMaximum(10000);

  canvas = new PixelTreeCanvas(&scrollArea, tc);

  connect(&scaleDown, SIGNAL(clicked()), canvas, SLOT(scaleDown()));
  connect(&scaleUp, SIGNAL(clicked()), canvas, SLOT(scaleUp()));

  QObject::connect(&compressionSB, SIGNAL(valueChanged(int)),
    canvas, SLOT(compressionChanged(int)));


  setAttribute(Qt::WA_QuitOnClose, true);
  setAttribute(Qt::WA_DeleteOnClose, true);

}

PixelTreeDialog::~PixelTreeDialog(void) {
  delete canvas;
}

PixelTreeCanvas::~PixelTreeCanvas(void) {
  freePixelList(pixelList);
  delete _image;

  if (time_arr)       delete [] time_arr;
  if (domain_arr)     delete [] domain_arr;
  if (domain_red_arr) delete [] domain_red_arr;
}

/// ***********************************


/// ******** PIXEL_TREE_CANVAS ********

PixelTreeCanvas::PixelTreeCanvas(QWidget* parent, TreeCanvas* tc)
  : QWidget(parent), _tc(tc), _na(tc->na), node_selected(-1)
{

  _sa = static_cast<QAbstractScrollArea*>(parentWidget());
  _vScrollBar = _sa->verticalScrollBar();

  _nodeCount = tc->stats.solutions + tc->stats.failures
                       + tc->stats.choices + tc->stats.undetermined;


  max_depth = tc->stats.maxDepth;
  int height = max_depth * _step;
  int width = _nodeCount * _step;

  _image = nullptr;

  /// scrolling business
  _sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  _sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  _sa->setAutoFillBackground(true);
  
  drawPixelTree();
  actuallyDraw();


  pixmap.fromImage(*_image);
  qlabel.setPixmap(pixmap);
  qlabel.show();

}

void
PixelTreeCanvas::paintEvent(QPaintEvent* event) {
  QPainter painter(this);

  actuallyDraw();
  painter.drawImage(0, 0, *_image);
  
}


void
PixelTreeCanvas::constructTree(void) {
  /// the depth is max_depth
  /// the width is _nodeCount / approx_size
  freePixelList(pixelList);

  vlines = ceil((float)_nodeCount / approx_size);

  if (time_arr != nullptr) delete time_arr;
  time_arr = new float[vlines];

  if (domain_arr != nullptr) delete domain_arr;
  domain_arr = new float[vlines];

  if (domain_red_arr != nullptr) delete domain_red_arr;
  domain_red_arr = new float[vlines];

  pixelList.resize(vlines);

  /// get a root
  VisualNode* root = (*_na)[0];

  vline_idx    = 0;
  node_idx     = 0;
  group_size   = 0;
  group_domain = 0;
  group_domain_red    = 0;
  group_size_nonempty = 0;

  alpha_factor = 100.0 / approx_size;

  exploreNew(root, 1);

  flush();

}

void
PixelTreeCanvas::freePixelList(std::vector<std::list<PixelData*>>& pixelList) {
  for (auto l : pixelList) {
    for (auto nodeData : l) {
      delete nodeData;
    }
    l.clear();
  }
  pixelList.clear();
}

void
PixelTreeCanvas::actuallyDraw() {
  delete _image;

  _sa->horizontalScrollBar()->setRange(0, vlines * _step - _sa->width() + 100);
  _sa->verticalScrollBar()->setRange(0, max_depth * _step - _sa->height());

  int xoff = _sa->horizontalScrollBar()->value();
  int yoff = _sa->verticalScrollBar()->value();

  int leftmost_x = xoff;
  int rightmost_x = xoff + _sa->width();

  pt_height = max_depth * _step;

  if (rightmost_x > pixelList.size() * _step) {
    rightmost_x = pixelList.size() * _step;
    // qDebug() << "rightmost_x = pixelList.size()\n";
  }

  int img_height = MARGIN + 
                   pt_height + _step +
                   MARGIN +
                   HIST_HEIGHT + _step + /// Time Histogram
                   MARGIN +
                   HIST_HEIGHT + _step + /// Domain Histogram
                   MARGIN +
                   HIST_HEIGHT + _step + /// Domain Reduction Histogram
                   MARGIN +
                   HIST_HEIGHT + _step + /// Node Rate Histogram
                   MARGIN;

  _image = new QImage(rightmost_x - leftmost_x, img_height, QImage::Format_RGB888);
  _image->fill(qRgb(255, 255, 255));

  this->resize(_image->width(), _image->height());

  int leftmost_vline = leftmost_x / _step;
  int rightmost_vline = rightmost_x / _step - 1;

  int* intencity_arr = new int[max_depth + 1];

  for (unsigned int vline = leftmost_vline; vline < rightmost_vline; vline++) {
    if (!pixelList[vline].empty()) {

      memset(intencity_arr, 0, (max_depth + 1)* sizeof(int));

      for (auto pixel : pixelList[vline]) {

        int xpos = (vline  - leftmost_vline) * _step;
        int ypos = pixel->depth() * _step;

        intencity_arr[pixel->depth()]++;

        /// draw pixel itself:
        int alpha = intencity_arr[pixel->depth()] * alpha_factor;
        drawPixel(xpos, ypos, _step, QColor::fromHsv(150, 100, 100 - alpha).rgba());

        /// draw green vertical line if solved:
        if (pixel->node()->getStatus() == SOLVED) {

          for (uint j = 0; j < pt_height; j++)
            if (_image->pixel(xpos, j) == qRgb(255, 255, 255))
              for (uint i = 0; i < _step; i++)
                _image->setPixel(xpos + i, j, qRgb(0, 255, 0));

        }
      }
      
    }
  }

  delete [] intencity_arr;

  /// All Histograms

  drawTimeHistogram(leftmost_vline, rightmost_vline);

  drawDomainHistogram(leftmost_vline, rightmost_vline);

  drawDomainReduction(leftmost_vline, rightmost_vline);

  drawNodeRate(leftmost_vline, rightmost_vline);
  
}

void
PixelTreeCanvas::drawPixelTree(void) {

  high_resolution_clock::time_point time_begin = high_resolution_clock::now();


  constructTree();

  high_resolution_clock::time_point time_end = high_resolution_clock::now();
  duration<double> time_span = duration_cast<duration<double>>(time_end - time_begin);
  std::cout << "Pixel Tree construction took: " << time_span.count() << " seconds." << std::endl;

}

void
PixelTreeCanvas::flush(void) {

  if (group_size == 0)
    return;

  if (group_size_nonempty == 0) {
    group_domain      = -1;
    group_domain_red  = -1;
    group_time        = -1;
  } else {
    group_domain        = group_domain / group_size_nonempty;
    group_domain_red    = group_domain_red / group_size_nonempty;
  }

  domain_arr[vline_idx]     = group_domain;
  domain_red_arr[vline_idx] = group_domain_red;
  time_arr[vline_idx]       =  group_time;

}

void
PixelTreeCanvas::exploreNew(VisualNode* node, int depth) {

  Data* data = _tc->getData();
  DbEntry* entry = data->getEntry(node->getIndex(*_na));
  DbEntry* parent = nullptr;

  assert(depth <= max_depth);

  pixelList[vline_idx].push_back(new PixelData(node_idx, node, depth));

  if (!entry) {
    qDebug() << "entry do not exist\n";
  } else {

    group_size_nonempty++;

    if (entry->parent_sid != ~0u) {
      parent = data->getEntry(node->getParent());

      group_domain_red += parent->domain - entry->domain;
    }

    group_time   += entry->node_time;
    group_domain += entry->domain;


  }

  group_size++;

  if (group_size == approx_size) {
    vline_idx++;
    group_size = 0;


    /// get average domain size for the group
    if (group_size_nonempty == 0) {
      group_domain      = -1;
      group_domain_red  = -1;
      group_time        = -1;
    } else {
      group_domain        = group_domain / group_size_nonempty;
      group_domain_red    = group_domain_red / group_size_nonempty;
      
    }


    time_arr[vline_idx]       = group_time;
    domain_arr[vline_idx]     = group_domain;
    domain_red_arr[vline_idx] = group_domain_red;
    
    group_time   = 0;
    group_domain = 0;
    group_domain_red = 0;
    group_size_nonempty = 0;

  }

  node_idx++;

  uint kids = node->getNumberOfChildren();
  for (uint i = 0; i < kids; ++i) {
    exploreNew(node->getChild(*_na, i), depth + 1);
  }


}


void
PixelTreeCanvas::exploreNode(VisualNode* node, int depth) {



  /// move to the right 
  if (group_size == approx_size) {




    /// record stuff for each vline
    // 
    // 


    // draw group
    // for (uint d = 1; d <= max_depth; d++) {
    //   if (intencity_arr[d - 1] > 0) {
    //     int alpha = intencity_arr[d - 1] * alpha_factor;
    //     if (node_idx - group_size < node_selected && node_selected <= node_idx) {
    //       drawPixel(x, d * _step, _step, qRgb(255, 0, 0));
    //     } else {
    //       drawPixel(x, d * _step, _step, QColor::fromHsv(150, 100, 100 - alpha).rgba());
    //     }
    //   }
        
    // }

  }


}

/// Draw time histogram underneath the pixel tree
void
PixelTreeCanvas::drawTimeHistogram(int l_vline, int r_vline) {

  drawHistogram(0, time_arr, l_vline, r_vline, qRgb(150, 150, 40));
}

void
PixelTreeCanvas::drawDomainHistogram(int l_vline, int r_vline) {
  drawHistogram(1, domain_arr, l_vline, r_vline, qRgb(150, 40, 150));
}

void
PixelTreeCanvas::drawDomainReduction(int l_vline, int r_vline) {
  drawHistogram(2, domain_red_arr, l_vline, r_vline, qRgb(40, 150, 150));
}

void
PixelTreeCanvas::drawHistogram(int idx, float* data, int l_vline, int r_vline, int color) {


  /// coordinates for the top-left corner
  int init_x = 0;
  int y = (pt_height + _step) + MARGIN + idx * (HIST_HEIGHT + MARGIN + _step);

  /// work out maximum value
  int max_value = 0;

  for (int i = 0; i < vlines; i++) {
    if (data[i] > max_value) max_value = data[i];
  }

  if (max_value <= 0) return; /// no data for this histogram

  float coeff = (float)HIST_HEIGHT / max_value;

  int zero_level = y + HIST_HEIGHT + _step;

  for (int i = l_vline; i < r_vline; i++) {
    int val = data[i] * coeff;

    /// horizontal line for 0 level
    for (int j = 0; j < _step; j++)
      _image->setPixel(init_x + (i - l_vline) * _step + j,
                       zero_level, 
                       qRgb(150, 150, 150));

    // qDebug() << "data[" << i << "]: " << data[i];

    // if (data[i] < 0) continue;

    drawPixel(init_x + (i - l_vline) * _step,
              y + HIST_HEIGHT - val,
              _step,
              color);

  }


}

void
PixelTreeCanvas::drawNodeRate(int l_vline, int r_vline) {
  Data* data = _tc->getData();
  std::vector<float>& node_rate = data->node_rate;
  std::vector<int>& nr_intervals = data->nr_intervals;

  int start_x = 0;
  int start_y = (pt_height + _step) + MARGIN + 3 * (HIST_HEIGHT + MARGIN + _step);

  float max_node_rate = *std::max_element(node_rate.begin(), node_rate.end());

  float coeff = (float)HIST_HEIGHT / max_node_rate;

  int zero_level = start_y + HIST_HEIGHT + _step;

  for (int i = l_vline; i < r_vline; i++) {
    for (int j = 0; j < _step; j++)
      _image->setPixel(start_x + (i - l_vline) * _step + j,
                       zero_level, 
                       qRgb(150, 150, 150));
  }

  int x = 0;

  for (int i = 1; i < nr_intervals.size(); i++) {
    float value = node_rate[i - 1] * coeff;
    int i_begin = ceil((float)nr_intervals[i-1] / approx_size);
    int i_end   = ceil((float)nr_intervals[i]   / approx_size);

    /// draw this interval?
    if (i_end < l_vline || i_begin > r_vline)
      continue;

    if (i_begin < l_vline) i_begin = l_vline;
    if (i_end   > r_vline) i_end   = r_vline;

    for (int x = i_begin; x < i_end; x++) {

      drawPixel(start_x + (x - l_vline) * _step,
                start_y + HIST_HEIGHT - value,
                _step,
                qRgb(40, 40, 150));
    }
  }




}

void
PixelTreeCanvas::scaleUp(void) {
  _step++;
  actuallyDraw();
  repaint();
}

void
PixelTreeCanvas::scaleDown(void) {
  if (_step <= 1) return;
  _step--;
  actuallyDraw();
  repaint();
}

void
PixelTreeCanvas::compressionChanged(int value) {
  qDebug() << "compression is set to: " << value;
  approx_size = value;
  drawPixelTree();
  repaint();
}

void
PixelTreeCanvas::drawPixel(int x, int y, int step, int color) {
  if (y < 0)
    return; /// TODO: fix later

  for (uint i = 0; i < _step; i++)
    for (uint j = 0; j < _step; j++)
      _image->setPixel(x + i, y + j, color);

}

void
PixelTreeCanvas::mousePressEvent(QMouseEvent* me) {

  int xoff = _sa->horizontalScrollBar()->value();
  int yoff = _sa->verticalScrollBar()->value();

  int x = me->x() + xoff;
  int y = me->y() + yoff;

  /// check boundaries:
  if (y > pt_height) return;

  // which node?

  int vline = x / _step;

  node_selected = vline * approx_size;

  selectNodesfromPT(node_selected, node_selected + approx_size);

  actuallyDraw();
  repaint();

}

void
PixelTreeCanvas::selectNodesfromPT(int first, int last) {

  struct Actions {

  private:
    NodeAllocator* _na;
    TreeCanvas* _tc;
    int node_id;
    int _first;
    int _last;
    void (Actions::*apply)(VisualNode*);
    bool _done;

  private:

    void selectOne(VisualNode* node) {
      _tc->setCurrentNode(node);
      _tc->centerCurrentNode();
    }

    void selectGroup(VisualNode* node) {
      node->dirtyUp(*_na);

      VisualNode* next = node;

      while (!next->isRoot() && next->isHidden()) {
        next->setHidden(false);
        next = next->getParent(*_na);
      }
    }

  public:

    Actions(NodeAllocator* na, TreeCanvas* tc, int first, int last)
    : _na(na), _tc(tc), _first(first), _last(last), _done(false) {}

    /// Initiate traversal
    void traverse() {
      node_id = 0;
      VisualNode* root = (*_na)[0];

      if (_last - _first == 1)
        apply = &Actions::selectOne;
      else {

        /// hide everything
        _tc->hideAll();
        root->setHidden(false);

        apply = &Actions::selectGroup;
      }

      explore(root);
    }



    void explore(VisualNode* node) {
      if (_done) return;

      // for current node:

      if (node_id >= _first) {
        if (node_id < _last) {

            (*this.*apply)(node);

        } else {
          /// stop traversal
          _done = true;
        }
      }

      node_id++;
      // for children
      uint kids = node->getNumberOfChildren();
      for (uint i = 0; i < kids; ++i) {
        explore(node->getChild(*_na, i));
      }

    }



  };

  Actions actions(_na, _tc, first, last);
  actions.traverse();
  _tc->update();

}

/// ***********************************