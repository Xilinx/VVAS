##############################
VVAS Infere Prediction
##############################

.. c:struct:: VvasInferPrediction

   Contains Inference meta data information of a frame

**Definition**

::

  struct VvasInferPrediction {
    uint64_t prediction_id;
    bool enabled;
    VvasBoundingBox bbox;
    VvasList* classifications;
    VvasTreeNode *node;
    bool bbox_scaled;
    char *obj_track_label;
    VvasClass model_class;
    char *model_name;
    int count;
    Pose14Pt pose14pt;
    Feature feature;
    Reid reid;
    Segmentation segmentation;
    TensorBuf *tb;
  };

**Members**

``prediction_id``
  A unique id for this specific prediction

``enabled``
  This flag indicates whether or not this prediction should be used for further inference

``bbox``
  Bouding box for this specific prediction

``classifications``
  linked list to classifications

``node``
  Address to tree data structure node

``bbox_scaled``
  bbox co-ordinates scaled to root node resolution or not

``obj_track_label``
  Track Label for the object

``model_class``
  Model class defined in vvas-core

``model_name``
  Model name

``count``
  A number element, used by model which give output a number

``pose14pt``
  Struct of the result returned by the posedetect/openpose network

``feature``
  Features of a face/road

``reid``
  Getting feature from an image

``segmentation``
  Segmentation data

``tb``
  Rawtensor data


.. c:struct:: VvasBoundingBox

   Contains information for box data for detected object

**Definition**

::

  struct VvasBoundingBox {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    VvasColorInfo box_color;
  };

**Members**

``x``
  horizontal coordinate of the upper position in pixels

``y``
  vertical coordinate of the upper position in pixels

``width``
  width of bounding box in pixels

``height``
  height of bounding box in pixels

``box_color``
  bounding box color





.. c:struct:: Pointf

   coordinate of point

**Definition**

::

  struct Pointf {
    float x;
    float y;
  };

**Members**

``x``
  horizontal coordinate of the upper position in pixels

``y``
  vertical coordinate of the upper position in pixels





.. c:struct:: Pose14Pt

   14 coordinate points to represented pose

**Definition**

::

  struct Pose14Pt {
    Pointf right_shoulder;
    Pointf right_elbow;
    Pointf right_wrist;
    Pointf left_shoulder;
    Pointf left_elbow;
    Pointf left_wrist;
    Pointf right_hip;
    Pointf right_knee;
    Pointf right_ankle;
    Pointf left_hip;
    Pointf left_knee;
    Pointf left_ankle;
    Pointf head;
    Pointf neck;
  };

**Members**

``right_shoulder``
  R_shoulder coordinate

``right_elbow``
  R_elbow coordinate

``right_wrist``
  R_wrist coordinate

``left_shoulder``
  L_shoulder coordinate

``left_elbow``
  L_elbow coordinate

``left_wrist``
  L_wrist coordinate

``right_hip``
  R_hip coordinate

``right_knee``
  R_knee coordinate

``right_ankle``
  R_ankle coordinate

``left_hip``
  L_hip coordinate

``left_knee``
  L_knee coordinate

``left_ankle``
  L_ankle coordinate

``head``
  Head coordinate

``neck``
  Neck coordinate





.. c:enum:: feature_type

   Enum for holding type of feature

**Constants**

``UNKNOWN_FEATURE``
  Unknown feature

``FLOAT_FEATURE``
  Float features

``FIXED_FEATURE``
  Fixed point features

``LANDMARK``
  Landmark

``ROADLINE``
  Roadlines

``ULTRAFAST``
  Points from Ultrafast model




.. c:enum:: road_line_type

   Enum for holding type of road line

**Constants**

``BACKGROUND``
  Background

``WHITE_DOTTED_LINE``
  White dotted line

``WHITE_SOLID_LINE``
  White solid line

``YELLOW_LINE``
  Yellow line




.. c:struct:: Feature

   The features of a road/person

**Definition**

::

  struct Feature {
    union {
      float float_feature[VVAS_MAX_FEATURES];
      int8_t fixed_feature[VVAS_MAX_FEATURES];
      Pointf road_line[VVAS_MAX_FEATURES];
      Pointf landmark[NUM_LANDMARK_POINT];
    };
    uint32_t line_size;
    enum feature_type type;
    enum road_line_type line_type;
  };

**Members**

``{unnamed_union}``
  anonymous

``float_feature``
  float features

``fixed_feature``
  fixed features

``road_line``
  points for drawing road lanes

``landmark``
  five key points on a human face

``line_size``
  Number of points in road_line

``type``
  enum to hold type of feature 

``line_type``
  enum to hold type of road lane





.. c:struct:: Reid

   Structure to gold reid model results

**Definition**

::

  struct Reid {
    uint32_t width;
    uint32_t height;
    uint64_t size;
    uint64_t type;
    void *data;
    bool (*free) (void *);
    bool (*copy) (const void *, void *);
  };

**Members**

``width``
  Width of output image

``height``
  Height of output image

``size``
  Size of output

``type``
  Type of Reid

``data``
  Reid output data

``free``
  function pointer to free data

``copy``
  function pointer to copy data





.. c:enum:: seg_type

   Enum for holding type of segmentation

**Constants**

``SEMANTIC``
  Semantic

``MEDICAL``
  Medical

``SEG3D``
  3D Segmentation




.. c:struct:: Segmentation

   Structure for storing segmentation related information

**Definition**

::

  struct Segmentation {
    enum seg_type type;
    uint32_t width;
    uint32_t height;
    char fmt[MAX_SEGOUTFMT_LEN];
    void *data;
    bool (*free) (void *);
    bool (*copy) (const void *, void *);
  };

**Members**

``type``
  enum to hold type of segmentation

``width``
  Width of output image

``height``
  Height of output image

``fmt``
  Segmentation output format

``data``
  Segmentation output data

``free``
  function pointer to free data

``copy``
  function pointer to copy data





.. c:struct:: TensorBuf

   Structure for storing Tensor related information

**Definition**

::

  struct TensorBuf {
    int size;
    void *ptr[20];
    void *priv;
    void (*free) (void **);
    void (*copy) (void **, void **);
    unsigned long int height;
    unsigned long int width;
    unsigned long int fmt;
    atomic_int ref_count;
  };

**Members**

``size``
  Size of output Tensors

``ptr``
  Pointers to output Tensors

``priv``
  Private structure

``free``
  function pointer to free data

``copy``
  function pointer to copy data

``height``
  Height of output image

``width``
  Width of output image

``fmt``
  Format of output image

``ref_count``
  Reference count





