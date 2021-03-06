// a minecraft clone in very few lines of code
// implements random world generation, rendering, and mining/building of blocks
// created in response to https://github.com/fogleman/Minecraft (which is 10x bigger in code)

include "std.lobster"
include "vec.lobster"
include "color.lobster"
include "3dhelpers.lobster"

lssize := 32 // uses lssize^3 blocks... make this bigger, and you'll anger the gods of framerate
inventory := [] // blocks we've mined
camera_position = vec_1 * lssize / 2
camera_yaw = 45

function inside(v): all(v > 0) & all(v < lssize - 1)    // keep the outer blocks empty for simplicity

cells := map(lssize) z:
            map(lssize) y:
                map(lssize) x:
                    v := [ x, y, z ]:xyz
                    h := simplex(v / (lssize + 0.0) + 11, 6, 1, 0.55)   // generate blocks using noise
                    solid := h > div(z, lssize) - 0.5 & inside(v)  // more likely to be solid the lower it is, and only when not on the outside
                    if(solid): solid = truncate((simplex([ x, y, z ] / (lssize + 0.0), 8, 1, 0.55) + 1) * 2) + 1 // pick material with noise too
                    solid

colors := [ color_dark_red, color_olive, color_green, color_dark_grey, color_grey ]
nbdirs := [ vec_xi, -vec_xi, vec_yi, -vec_yi, vec_zi, -vec_zi ]
nbpolys := map([ "4576", "0231", "2673", "0154", "1375", "0462" ]) s: map(s) c: map(3) i: and(c, shl(1, 2 - i)) != 0

fatal(gl_window("LobsterCraft(tm)", 800, 600))

while(gl_frame()):   
    if(gl_wentdown("escape")): return
    gl_clear(color_light_blue)
    gl_cursor(0)
    gl_perspective(70, 1, 1000)
    camera_FPS_update("w", "a", "s", "d", 10, 4, true)
    camera_FPS_view()
    gl_light(camera_position)
    gl_blend(0)
    gl_setshader("phong")
    for(cells) zl, z: for(zl) yl, y: for(yl) e, x:  // for all cells in the world
        if(e):  // if this cell is solid
            v := [ x, y, z ]:xyz
            for(nbdirs) nv, i:
                if(!cells[nv + v]):  // and neighbor is empty 
                    gl_color(colors[e - 1]) // we do this here rather than outside the loop because there's less visible faces than total solid cubes
                    gl_translate(v):
                        gl_polygon(nbpolys[i])  // draw face
    first_solid := nil  // find the first solid and last empty block we're looking at
    last_empty := nil
    camvec := vecfromyawpitch(camera_yaw, camera_pitch, 1, 0)
    for(100) i:  // look at most 10 cubes ahead
        pos := map(camera_position + camvec * (i / 10.0)): truncate(_)
        if(inside(pos) & !first_solid):
            if(cells[pos]): first_solid = pos
            else: last_empty = pos
    if(gl_wentdown("mouse1") & last_empty & inventory.length): cells[last_empty] = inventory.pop()   // LMB places blocks
    if(gl_wentdown("mouse3") & first_solid): inventory.push(cells[first_solid]); cells[first_solid] = 0   // RMB removes blocks
    gl_ortho()
    gl_setshader("color")
    gl_color(color_white)
    gl_translate(gl_windowsize() / 2):
        gl_circle(5, 20)    // simplified crosshairs
