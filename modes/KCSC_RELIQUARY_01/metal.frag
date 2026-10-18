#ifdef GL_ES
precision mediump float;
#endif
uniform float u_time;
uniform float u_hit;
uniform float u_finish;
uniform float u_light;
varying vec3 v_normal;
varying vec3 v_pos;
varying float v_face;
void main() {
    vec3 n=normalize(v_normal);
    vec3 view=normalize(vec3(0.0,0.0,5.6)-v_pos);
    vec3 key=normalize(vec3(-0.65+0.18*sin(u_time*0.19),0.80,1.4));
    vec3 rim=normalize(vec3(1.0,-0.15,0.4));
    float diffuse=max(dot(n,key),0.0);
    float edge=max(dot(n,rim),0.0);
    float spec=pow(max(dot(n,normalize(key+view)),0.0),38.0);
    float rimSpec=pow(max(dot(n,normalize(rim+view)),0.0),22.0);
    vec3 cool=mix(vec3(0.10,0.53,0.72),vec3(0.55,0.31,0.12),u_finish);
    vec3 face=mix(vec3(0.76,0.84,0.86),vec3(0.92,0.82,0.65),u_finish);
    vec3 body=mix(vec3(0.025,0.055,0.070),vec3(0.075,0.050,0.028),u_finish);
    vec3 base=mix(body,face,v_face);
    float satin=0.72+0.28*smoothstep(-0.8,0.75,v_pos.y+v_pos.x*0.22);
    vec3 c=base*(0.30+0.78*diffuse)*mix(1.0,satin,v_face);
    c+=cool*edge*(0.26+u_light*0.55+u_hit*0.38)*(1.0-v_face*0.72);
    c+=vec3(0.80,0.90,1.0)*spec*(0.20+0.22*u_light);
    c+=cool*rimSpec*(0.55+0.40*u_light);
    // Small continuous machining reflection; no strobe, noise, or logo displacement.
    float band=exp(-pow((v_pos.y*0.42+v_pos.x*0.24-0.22*sin(u_time*0.23))*3.2,2.0));
    float strip=exp(-pow((v_pos.y-v_pos.x*.18-.25-.12*sin(u_time*.17))*6.0,2.0));
    c+=(vec3(0.045)*band+vec3(0.12,0.14,0.15)*strip)*v_face;
    gl_FragColor=vec4(clamp(c,0.0,1.0),1.0);
}
