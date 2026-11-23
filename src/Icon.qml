import QtQuick

// A line icon from icons/, tinted to any color by the C++ IconProvider.
// Wrapped in an Item so layouts see `size`, not the image's pixel size.
Item {
    id: icon
    property string name
    property color color: "#e6e6e6"
    property int size: 18

    implicitWidth: size
    implicitHeight: size

    Image {
        anchors.fill: parent
        sourceSize: Qt.size(icon.size * 2, icon.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        source: icon.name ? "image://icon/" + icon.name + "?color=" + encodeURIComponent(icon.color.toString()) : ""
    }
}
