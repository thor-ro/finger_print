import FreeCAD as App
import Part

doc = App.ActiveDocument
if doc is None:
    doc = App.newDocument("Enclosure")

# Dimensions & Tolerances
wall = 2.0
clearance = 0.2  # Gap for snap fit / lip
lip_thickness = 1.0
lip_height = 2.5

# Component Dimensions 
bat_l, bat_w, bat_h = 65.0, 23.0, 34.0
esp_l, esp_w, esp_h = 23.5, 18.0, 5.0
sensor_dia = 19.2
usb_w, usb_h = 10.0, 4.0

# Layout Calculations
# We will place the battery and the ESP end-to-end to keep a slim profile
inner_l = bat_l + esp_l + 8.0  # Buffer for wiring
inner_w = max(bat_w, esp_w) + 4.0
inner_h = max(bat_h, esp_h) + 2.0  # Leave headroom for sensor depth on top

outer_l = inner_l + 2 * wall
outer_w = inner_w + 2 * wall
outer_h = inner_h + 2 * wall

# 1. Create solid blocks
outer_box = Part.makeBox(outer_l, outer_w, outer_h)
inner_box = Part.makeBox(inner_l, inner_w, inner_h)
inner_box.translate(App.Vector(wall, wall, wall))

# Main hollow shell
shell = outer_box.cut(inner_box)

# 2. Split into Base and Lid (Snap-fit design)
split_z = outer_h - 10.0  # Lid is 10mm tall

base_tool = Part.makeBox(outer_l, outer_w, split_z)
base = shell.common(base_tool)

lid_tool = Part.makeBox(outer_l, outer_w, 10.0)
lid_tool.translate(App.Vector(0, 0, split_z))
lid = shell.common(lid_tool)

# 3. Add a Lip to the Base for the Snap-Fit
lip_outer = Part.makeBox(inner_l - clearance*2, inner_w - clearance*2, lip_height)
lip_outer.translate(App.Vector(wall + clearance, wall + clearance, split_z))

lip_inner = Part.makeBox(inner_l - clearance*2 - lip_thickness*2, inner_w - clearance*2 - lip_thickness*2, lip_height)
lip_inner.translate(App.Vector(wall + clearance + lip_thickness, wall + clearance + lip_thickness, split_z))

lip_frame = lip_outer.cut(lip_inner)
base = base.fuse(lip_frame)

# 4. Fingerprint Cutout on the Lid
sensor_r = sensor_dia / 2.0
fp_hole = Part.makeCylinder(sensor_r, wall + 2.0)
# Center it over the battery portion
fp_hole.translate(App.Vector(wall + bat_l/2, outer_w/2, split_z + 10.0 - wall - 1))
lid = lid.cut(fp_hole)

# 5. USB-C Cutout on the Base (side where ESP is located)
# Place it on the right side of the box (x = outer_l)
usb_hole = Part.makeBox(wall + 4, usb_w, usb_h)
# Aligning with ESP side
usb_hole.translate(App.Vector(outer_l - wall - 2, outer_w/2 - usb_w/2, wall + 2.0))
base = base.cut(usb_hole)

# Provide descriptive names
Part.show(base, "Enclosure_Base")
Part.show(lid, "Enclosure_Lid")

App.ActiveDocument.recompute()
