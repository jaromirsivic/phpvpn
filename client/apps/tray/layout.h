#pragma once
namespace phpvpn::tray {
struct Size { int width = 0; int height = 0; };
struct Rect { int left = 0; int top = 0; int right = 0; int bottom = 0; };
struct EditorLayout { Rect textArea; Rect saveButton; Rect resetButton; Rect cancelButton; };
int scaleForDpi(int pixels, int dpi);
Size defaultEditorClientSize(int dpi);
Size clampToWorkArea(Size desired, Rect workArea, int dpi);
Rect centerInWorkArea(Size size, Rect workArea);
EditorLayout computeEditorLayout(Size client, int dpi, bool editable);
Rect exposedParentRegion(Size oldClient, Size newClient,
                         const EditorLayout& oldLayout, const EditorLayout& newLayout);
}  // namespace phpvpn::tray
