#ifdef GL_ES
precision highp float;
#endif
attribute vec4 position;
attribute vec3 normal;
uniform vec2 u_resolution;
uniform vec3 u_pose;
uniform float u_depth;
uniform float u_scale;
uniform float u_clipY;
varying vec3 v_normal;
varying vec3 v_pos;
varying float v_face;

vec3 rotateObject(vec3 p) {
    float cx=cos(u_pose.x), sx=sin(u_pose.x);
    float cy=cos(u_pose.y), sy=sin(u_pose.y);
    float cz=cos(u_pose.z), sz=sin(u_pose.z);
    p=vec3(p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z);
    p=vec3(cy*p.x+sy*p.z,p.y,-sy*p.x+cy*p.z);
    return vec3(cz*p.x-sz*p.y,sz*p.x+cz*p.y,p.z);
}
void main() {
    vec3 p=position.xyz;
    // Only the straight wall/body uses variable depth. Bevel geometry stays rigid.
    if(p.z<0.0) p.z*=u_depth;
    p.z+=u_depth*0.5;
    p=rotateObject(p);
    v_normal=rotateObject(normal);
    v_pos=p;
    v_face=step(0.995,normal.z);
    float camera=5.6;
    float w=camera-p.z;
    float aspect=u_resolution.x/u_resolution.y;
    // Explicit perspective and clip depth; independent of OF's 2D projection.
    gl_Position=vec4(p.x*u_scale*camera/aspect,p.y*u_scale*camera*u_clipY,-p.z,w);
}
