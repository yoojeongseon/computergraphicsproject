#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <GL/glu.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y, z; } Vec3;
typedef struct { unsigned int a, b, c; } Tri;
typedef struct {
    Vec3* verts; size_t vcount;
    Tri* tris;  size_t tcount;
} Model;

static Model g_model;
static int   g_win_w = 800, g_win_h = 800;

/* ====== 이동 상태 (WASD) ====== */
static float obj_x = 0.0f, obj_z = 0.0f;
static int keyW = 0, keyA = 0, keyS = 0, keyD = 0;
static const float MOVE_SPEED = 1.5f;   // 초당 이동 거리

static int last_time_ms = 0;

/* ====== 유틸 ====== */
static void die(const char* msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}
static void* xrealloc(void* p, size_t sz) {
    void* t = realloc(p, sz);
    if (!t) die("Out of memory");
    return t;
}
static int parse_index_token(const char* tok) {
    char buf[64];
    size_t k = 0;
    while (*tok && *tok != '/' && k + 1 < sizeof(buf)) {
        buf[k++] = *tok++;
    }
    buf[k] = 0;
    return atoi(buf);
}

/* ====== OBJ 로더 (cube.obj) ====== */
static void load_obj(const char* path, Model* m) {
    if (!path || !m) die("load_obj: bad args");
    FILE* fp = NULL;
#ifdef _MSC_VER
    errno_t err = fopen_s(&fp, path, "r");
    if (err != 0 || !fp) {
        fprintf(stderr, "Failed to open OBJ: %s\n", path);
        die("OBJ open error");
    }
#else
    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open OBJ: %s\n", path);
        die("OBJ open error");
    }
#endif

    size_t vcap = 256, tcap = 512, vcnt = 0, tcnt = 0;
    Vec3* verts = (Vec3*)calloc(vcap, sizeof(Vec3));
    Tri* tris = (Tri*)calloc(tcap, sizeof(Tri));
    if (!verts || !tris) {
        if (fp) fclose(fp);
        die("Out of memory");
    }

    char line[1024];
    while (fp && fgets(line, sizeof(line), fp)) {
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            float x, y, z;
            if (sscanf(line + 1, "%f %f %f", &x, &y, &z) == 3) {
                if (vcnt + 1 > vcap) {
                    vcap *= 2;
                    verts = (Vec3*)xrealloc(verts, vcap * sizeof(Vec3));
                }
                verts[vcnt++] = (Vec3){ x, y, z };
            }
        }
        else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            char t0[64] = { 0 }, t1[64] = { 0 }, t2[64] = { 0 }, t3[64] = { 0 };
            int n = sscanf(line + 1, " %63s %63s %63s %63s", t0, t1, t2, t3);
            if (n >= 3) {
                int i0 = parse_index_token(t0);
                int i1 = parse_index_token(t1);
                int i2 = parse_index_token(t2);
                if (i0 && i1 && i2) {
                    if (tcnt + 1 > tcap) {
                        tcap *= 2;
                        tris = (Tri*)xrealloc(tris, tcap * sizeof(Tri));
                    }
                    tris[tcnt++] = (Tri){ (unsigned)(i0 - 1),
                                          (unsigned)(i1 - 1),
                                          (unsigned)(i2 - 1) };
                }
                if (n == 4) {
                    int i3 = parse_index_token(t3);
                    if (i3) {
                        if (tcnt + 1 > tcap) {
                            tcap *= 2;
                            tris = (Tri*)xrealloc(tris, tcap * sizeof(Tri));
                        }
                        tris[tcnt++] = (Tri){ (unsigned)(i0 - 1),
                                              (unsigned)(i2 - 1),
                                              (unsigned)(i3 - 1) };
                    }
                }
            }
        }
    }
    if (fp) fclose(fp);
    if (vcnt == 0 || tcnt == 0) die("OBJ has no geometry");

    m->verts = verts;  m->vcount = vcnt;
    m->tris = tris;   m->tcount = tcnt;
}

/* ====== 모델 정규화 (크기/위치 맞추기) ====== */
static void normalize_model(Model* m) {
    Vec3 minv = m->verts[0];
    Vec3 maxv = m->verts[0];

    for (size_t i = 1; i < m->vcount; ++i) {
        Vec3 v = m->verts[i];
        if (v.x < minv.x) minv.x = v.x;
        if (v.y < minv.y) minv.y = v.y;
        if (v.z < minv.z) minv.z = v.z;
        if (v.x > maxv.x) maxv.x = v.x;
        if (v.y > maxv.y) maxv.y = v.y;
        if (v.z > maxv.z) maxv.z = v.z;
    }

    float dx = maxv.x - minv.x;
    float dy = maxv.y - minv.y;
    float dz = maxv.z - minv.z;
    float maxd = dx;
    if (dy > maxd) maxd = dy;
    if (dz > maxd) maxd = dz;
    if (maxd <= 0.0f) maxd = 1.0f;

    float sx = 1.0f / maxd;
    float sy = 1.0f / maxd;
    float sz = 1.0f / maxd;

    float cx = 0.5f * (minv.x + maxv.x);
    float cy = 0.5f * (minv.y + maxv.y);
    float cz = 0.5f * (minv.z + maxv.z);

    for (size_t i = 0; i < m->vcount; ++i) {
        m->verts[i].x = (m->verts[i].x - cx) * sx;
        m->verts[i].y = (m->verts[i].y - cy) * sy;
        m->verts[i].z = (m->verts[i].z - cz) * sz;
    }
}

/* ====== 모델 그리기 ====== */
static void draw_model(const Model* m) {
    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < m->tcount; ++i) {
        Tri t = m->tris[i];
        Vec3 v0 = m->verts[t.a];
        Vec3 v1 = m->verts[t.b];
        Vec3 v2 = m->verts[t.c];
        glVertex3f(v0.x, v0.y, v0.z);
        glVertex3f(v1.x, v1.y, v1.z);
        glVertex3f(v2.x, v2.y, v2.z);
    }
    glEnd();
}

/* ====== XYZ 축 그리기 ====== */
static void draw_axes(void) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    // X (red)
    glColor3f(1, 0, 0);
    glVertex3f(0, 0, 0); glVertex3f(1.5f, 0, 0);
    // Y (green)
    glColor3f(0, 1, 0);
    glVertex3f(0, 0, 0); glVertex3f(0, 1.5f, 0);
    // Z (blue)
    glColor3f(0, 0, 1);
    glVertex3f(0, 0, 0); glVertex3f(0, 0, 1.5f);
    glEnd();
}

/* ====== 투영 ====== */
static void set_projection(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (h == 0) ? 1.0f : (float)w / (float)h;
    gluPerspective(45.0, aspect, 0.1, 100.0);
}

/* ====== 렌더링 ====== */
static void display(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int half_w = g_win_w / 2;
    int right_w = g_win_w - half_w;

    /* ===== 왼쪽: 1인칭 뷰 ===== */
    glViewport(0, 0, half_w, g_win_h);
    set_projection(half_w, g_win_h);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* 카메라 위치 = 큐브 위치(약간 위), 항상 -Z 방향을 본다고 가정 */
    double eye_x = obj_x;
    double eye_y = 0.5;      // 눈 높이
    double eye_z = obj_z;
    double center_x = obj_x;
    double center_y = 0.5;
    double center_z = obj_z - 1.0;  // 앞쪽 (월드 -Z)

    gluLookAt(eye_x, eye_y, eye_z,
        center_x, center_y, center_z,
        0.0, 1.0, 0.0);

    // 1인칭 뷰에서는 큐브 자기 자신은 안 그리기 (안 보이는 게 자연스럽게)
    glDisable(GL_LIGHTING);
    draw_axes();   // 원점 축만 보이면서 우리가 움직이는 느낌 확인

    /* ===== 오른쪽: 3인칭 뷰(기존) ===== */
    glViewport(half_w, 0, right_w, g_win_h);
    set_projection(right_w, g_win_h);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* 살짝 위에서 내려다보는 카메라 */
    gluLookAt(3.0, 2.0, 3.0,
        0.0, 0.0, 0.0,
        0.0, 1.0, 0.0);

    glDisable(GL_LIGHTING);
    draw_axes();

    glPushMatrix();
    glTranslatef(obj_x, 0.0f, obj_z);
    glColor3f(0.7f, 0.7f, 0.7f);
    draw_model(&g_model);
    glPopMatrix();

    glutSwapBuffers();
}

/* ====== 리사이즈 ====== */
static void reshape(int w, int h) {
    g_win_w = w;
    g_win_h = h;
    glViewport(0, 0, w, h);    // 일단 전체, 실제 뷰포트는 display에서 나눔
}

/* ====== 키 입력 (누를 때) ====== */
static void keyboard(unsigned char key, int x, int y) {
    (void)x; (void)y;
    if (key == 'w' || key == 'W') keyW = 1;
    else if (key == 's' || key == 'S') keyS = 1;
    else if (key == 'a' || key == 'A') keyA = 1;
    else if (key == 'd' || key == 'D') keyD = 1;
    else if (key == 27 || key == 'q' || key == 'Q') exit(0);
}

/* ====== 키 입력 (뗄 때) ====== */
static void keyboard_up(unsigned char key, int x, int y) {
    (void)x; (void)y;
    if (key == 'w' || key == 'W') keyW = 0;
    else if (key == 's' || key == 'S') keyS = 0;
    else if (key == 'a' || key == 'A') keyA = 0;
    else if (key == 'd' || key == 'D') keyD = 0;
}

/* ====== 게임처럼 계속 움직이게 하는 idle ====== */
static void idle_update(void) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    if (last_time_ms == 0) last_time_ms = now;
    float dt = (now - last_time_ms) / 1000.0f;
    last_time_ms = now;

    float step = MOVE_SPEED * dt;
    if (keyW) obj_z -= step;
    if (keyS) obj_z += step;
    if (keyA) obj_x -= step;
    if (keyD) obj_x += step;

    glutPostRedisplay();
}

/* ====== 초기화 ====== */
static void init_gl(void) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);   // 흰 배경
}

/* ====== main ====== */
int main(int argc, char** argv) {
    const char* objpath = "cube.obj";   /* 여기서 cube.obj 사용 */
    if (argc >= 2) objpath = argv[1];

    load_obj(objpath, &g_model);
    normalize_model(&g_model);

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(g_win_w, g_win_h);
    glutCreateWindow("Cube Dual View (1st & 3rd) + WASD");

    GLenum e = glewInit();
    if (e != GLEW_OK) die("glewInit failed");

    init_gl();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboard_up);
    glutIdleFunc(idle_update);

    glutMainLoop();
    return 0;
}