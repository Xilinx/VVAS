.. _vvas_overlay_metadata:


***********************
VVAS Overlay Metadata
***********************

VVAS overlay metadata structure hold the information of geometric shapes and text need to be overlaid on video frames. VVAS overlay plugin parses the overlay metadata structures to overlay information on the frames. An intermediate plugin is required for converting metadata generated from upstream plugins like infer, segmentation or optical flow plugins to overlay metadata for displaying information on frames. Currently supported structures in gstvvasoverlaymeta are rectangles, text, lines, arrows, circles and polygons. For displaying text, text need to be display must be copied into the text structure.

The GStreamer plug-ins can set and get overlay metadata from the GstBuffer by using the gst_buffer_add_meta () API and gst_buffer_get_meta () API, respectively.

.. _GstOverlayMeta:

================
GstOverlayMeta
================

GstOverlayMeta structure holds VvasOverlayShapeInfo of vvas_core which intern stores the information of different geometric structures and text. The structural information of different shapes, VvasOverlayShapeInfo and GstOverlayMeta are as described below:


.. code-block::


        typedef enum  {
        ARROW_DIRECTION_START ,
        ARROW_DIRECTION_END,
        ARROW_DIRECTION_BOTH_ENDS
      } VvasOverlayArrowDirection;

      typedef struct {
        int32_t x;
        int32_t y;
      } VvasOverlayCoordinates;

      typedef struct {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t alpha;
      } VvasOverlayColorData;

      typedef struct {
        uint32_t font_num;
        float font_size;
        VvasOverlayColorData font_color;
      } VvasOverlayFontData;

      typedef struct {
        VvasOverlayCoordinates points;
        uint32_t width;
        uint32_t height;
        uint32_t thickness;
        VvasOverlayColorData rect_color;
        uint32_t apply_bg_color;
        VvasOverlayColorData bg_color;
      } VvasOverlayRectParams;

      typedef struct {
        VvasOverlayCoordinates points;
        char * disp_text;
        uint32_t bottom_left_origin;
        VvasOverlayFontData text_font;
        uint32_t apply_bg_color;
        VvasOverlayColorData bg_color;
      } VvasOverlayTextParams;

      typedef struct {
        VvasOverlayCoordinates start_pt;
        VvasOverlayCoordinates end_pt;
        uint32_t thickness;
        VvasOverlayColorData line_color;
      } VvasOverlayLineParams;

      typedef struct {
        VvasOverlayCoordinates start_pt;
        VvasOverlayCoordinates end_pt;
        VvasOverlayArrowDirection arrow_direction;
        uint32_t thickness;
        float tipLength;
        VvasOverlayColorData line_color;
      } VvasOverlayArrowParams;

      typedef struct {
        VvasOverlayCoordinates center_pt;
        uint32_t radius;
        uint32_t thickness;
        VvasOverlayColorData circle_color;
      } VvasOverlayCircleParams;

      typedef struct {
        VvasList * poly_pts;
        uint32_t num_pts;
        uint32_t thickness;
        VvasOverlayColorData poly_color;
      } VvasOverlayPolygonParams;

      /**
      * GstVvasOverlayMeta:
      * @num_rects: number of bounding boxes
      * @num_text: number of text boxes
      * @num_lines: number of lines
      * @num_arrows: number of arrows
      * @num_circles: number of circles
      * @num_polys: number of polygons
      * @rect_params: structure for holding rectangles information
      * @text_params: structure for holding text information
      * @line_params: structure for holding lines information
      * @arrow_params: structure for holding arrows information
      * @circle_params: structure for holding circles information
      * @polygon_params: structure for holding polygons information
      */
      typedef struct {
        GstMeta meta;
        int num_rects;
        int num_text;
        int num_lines;
        int num_arrows;
        int num_circles;
        int num_polys;

        VvasList *rect_params;
        VvasList *text_params;
        VvasList *line_params;
        VvasList *arrow_params;
        VvasList *circle_params;
        VvasList *polygon_params;
      } VvasOverlayShapeInfo;

      typedef struct _GstVvasOverlayMeta GstVvasOverlayMeta;
      struct _GstVvasOverlayMeta {
        GstMeta meta;

        VvasOverlayShapeInfo shape_info;
      };

