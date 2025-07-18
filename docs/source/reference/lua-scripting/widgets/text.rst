Text Widget
===========

Displays text.

Properties
----------

.. list-table::
  :header-rows: 1
  :widths: 20 20 60

  * - name
    - type (default value)
    - description
  * - position
    - util.vector2 (0, 0)
    - | Offsets the position of the widget from its parent's
      | top-left corner in pixels.
  * - size
    - util.vector2 (0, 0)
    - Increases the widget's size in pixels.
  * - relativePosition  
    - util.vector2 (0, 0)
    - | Offsets the position of the widget from its parent's
      | top-left corner as a fraction of the parent's size.
  * - relativeSize
    - util.vector2 (0, 0)
    - Increases the widget's size by a fraction of its parent's size.
  * - anchor
    - util.vector2 (0, 0)
    - | Offsets the widget's position by a fraction of its size.
      | Useful for centering or aligning to a corner.
  * - visible
    - boolean (true)
    - Defines if the widget is visible
  * - propagateEvents
    - boolean (true)
    - Allows base widget events to propagate to the widget's parent.
  * - alpha
    - number (1.0)
    - | Set the opacity of the widget and its contents.
      | If `inheritAlpha` is set to `true`, this becomes the maximum alpha value the widget can take.
  * - inheritAlpha
    - boolean (true)
    - | Modulate `alpha` with parents `alpha`.
      | If the parent has `inheritAlpha` set to `true`, the value after modulating is passed to the child.
  * - autoSize
    - boolean (true)
    - | Adjusts this widget's size to fit the text exactly.
      | Ignores `size` and `relativeSize`.
  * - text
    - string ('')
    - The text to display.
  * - textSize
    - number (10)
    - The size of the text.
  * - textColor
    - util.color (``rgb(0, 0, 0)``)
    - The color of the text.
  * - multiline
    - boolean (false)
    - Whether to render text on multiple lines.
  * - wordWrap
    - boolean (false)
    - Whether to break text into lines to fit the widget's width.
  * - textAlignH
    - ui.ALIGNMENT (Start)
    - Horizontal alignment of the text.
  * - textAlignV
    - ui.ALIGNMENT (Start)
    - Vertical alignment of the text.
  * - textShadow
    - boolean (false)
    - Whether to render a shadow behind the text.
  * - textShadowColor
    - util.color (``rgb(0, 0, 0)``)
    - The color of the text shadow.

Events
------

Base widget events are special, they can propagate up to the parent widget.
This can be prevented by changing the `propagateEvents` property, or by assigning an  event handler.
The event is still allowed to propagate if the event handler returns `true`.

.. list-table::
  :header-rows: 1
  :widths: 20 20 60

  * - name
    - first argument type
    - description
  * - keyPress
    - `KeyboardEvent <../openmw_input.html##(KeyboardEvent)>`_
    - A key was pressed with this widget in focus
  * - keyRelease
    - `KeyboardEvent <../openmw_input.html##(KeyboardEvent)>`_
    - A key was released with this widget in focus
  * - mouseMove
    - `MouseEvent <../openmw_ui.html##(MouseEvent)>`_
    - | Mouse cursor moved on this widget
      | `MouseEvent.button` is the mouse button being held
      | (nil when simply moving, and not dragging)
  * - mouseClick
    - nil
    - Widget was clicked with left mouse button
  * - mouseDoubleClick
    - nil
    - Widget was double clicked with left mouse button
  * - mousePress  
    - `MouseEvent <../openmw_ui.html##(MouseEvent)>`_
    - A mouse button was pressed on this widget
  * - mouseRelease  
    -  `MouseEvent <../openmw_ui.html##(MouseEvent)>`_
    - A mouse button was released on this widget
  * - focusGain
    - nil
    - Widget gained focus (either through mouse or keyboard)
  * - focusLoss
    - nil
    - Widget lost focus
  * - textInput
    - string
    - Text input with this widget in focus
