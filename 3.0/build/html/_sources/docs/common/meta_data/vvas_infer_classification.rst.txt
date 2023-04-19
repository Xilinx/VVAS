############################
VVAS Infer Classification
############################

.. c:struct:: VvasInferClassification

   Contains information on classification for each object

**Definition**

::

  struct VvasInferClassification {
    uint64_t classification_id;
    int32_t class_id;
    double class_prob;
    char* class_label;
    int32_t num_classes;
    double* probabilities;
    char** labels;
    VvasColorInfo label_color;
  };

**Members**

``classification_id``
  A unique id associated to this classification

``class_id``
  The numerical id associated to the assigned class

``class_prob``
  The resulting probability of the assigned class. Typically ranges between 0 and 1

``class_label``
  The label associated to this class or NULL if not available

``num_classes``
  The total amount of classes of the entire prediction

``probabilities``
  The entire array of probabilities of the prediction

``labels``
  The entire array of labels of the prediction. NULL if not available

``label_color``
  The color of labels



.. c:struct:: VvasColorInfo

   Contains information for color of the detected object

**Definition**

::

  struct VvasColorInfo {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
  };

**Members**

``red``
  R color component

``green``
  G color component

``blue``
  B color component

``alpha``
  Transparency




