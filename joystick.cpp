#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <math.h>
#include <iostream>
#include <vector>


// Identificación del joystick
#define JOYSTICK_DEVNAME "/dev/input/js0"

// Identificación del teclado
#define KEYBOARD_DEVNAME "/dev/input/event3"

// Estados para la programación de las macros
#define INICIAL 0
#define PROGRAMANDO 1
#define ESPERANDO_BOTON_PROGRAMACION 2

// Botones programables del joystick
char BOTONES_PROGRAMABLES[] = "YBAX";

// Manejadores de los ficheros de los dispositivos
static int joystick_fd = -1;
static int keyboard_fd = -1;
static int uinput_mouse_fd = -1;
static int uinput_keyboard_fd = -1;

typedef struct{
    int _x;
    int _y;
} Coordenadas;

static Coordenadas coord;
static int num_of_axis=0, num_of_buttons=0;
static char *button=NULL, name_of_joystick[80];

int open_joystick() {
	joystick_fd = open(JOYSTICK_DEVNAME, O_RDONLY | O_NONBLOCK);
	if (joystick_fd < 0)
		return joystick_fd;

	// Configuración del dispositivo
	ioctl(joystick_fd, JSIOCGAXES, &num_of_axis);
	ioctl(joystick_fd, JSIOCGBUTTONS, &num_of_buttons);
	ioctl(joystick_fd, JSIOCGNAME(80), &name_of_joystick);
	num_of_axis = num_of_axis & 0xFF;
	num_of_buttons = num_of_buttons & 0xFF;
	printf("Joystick detectado: %s\n\n", name_of_joystick);

	button = (char *) calloc( num_of_buttons, sizeof(char));
	return joystick_fd;
}

int read_joystick_event(struct js_event *jse) {
	int bytes;

	bytes = read(joystick_fd, jse, sizeof(*jse)); 

	if (bytes == -1)
		return 0;

	if (bytes == sizeof(*jse))
		return 1;

	printf("Unexpected bytes from joystick:%d\n", bytes);

	return -1;
}

int open_keyboard() {
	keyboard_fd = open(KEYBOARD_DEVNAME, O_RDONLY | O_NONBLOCK);
	return keyboard_fd;
}

void close_keyboard() {
	close(keyboard_fd);
}

void close_joystick() {
	close(joystick_fd);
}

int get_joystick_status(int *id) {
	int rc;
	struct js_event jse;
	if (joystick_fd < 0)
		return -1;

	while ((rc = read_joystick_event(&jse) == 1)) {
		jse.type &= ~JS_EVENT_INIT; // Ignora los eventos sintéticos
		if (jse.type == JS_EVENT_AXIS) {
			switch (jse.number) {
			case 1: coord._y = jse.value;  // Control analógico eje vertical - eje 1
			*id = 0;
			break;
			case 0: coord._x = jse.value;  // Control analógico eje horizontal - eje 0
			*id = 0;
			break;
			default:
			break;
			}
			return JS_EVENT_AXIS;
		} else if (jse.type == JS_EVENT_BUTTON) {
			button [jse.number] = jse.value;
			*id = jse.number;
			return JS_EVENT_BUTTON;
		}
		return 0;
	}
	return 0;
}

int get_keyboard_status(std::vector<int> &v) {
    int rc;
    int bytes;
    struct input_event eventos[64];
    int num_events;

	if (keyboard_fd < 0){
		return -1;
    }

    bytes = read(keyboard_fd, &eventos, sizeof(struct input_event) *64);

    if (bytes >= (int) sizeof(struct input_event))
        rc = 1;
    else
        rc = -1;

    if (rc == 1){
        num_events = bytes/sizeof(struct input_event);
        for (int i = 0;i<num_events;i++){   			  	
            if (eventos[i].type == EV_KEY && eventos[i].value == 1  && eventos[i].code){				
                v.push_back(eventos[i].code);
            } 			
        }
    }
    return rc;
}

int uinput_keyboard_init(void) {
    struct uinput_user_dev mi_teclado;

    uinput_keyboard_fd = open("/dev/uinput", O_WRONLY | O_NDELAY);
    if (uinput_keyboard_fd<0) {
	    perror("No puedo abrir /dev/input/uinput :\n -¿Tienes permisos de lectura/escritura?\n");
		exit(EXIT_FAILURE);
	}
    
    memset(&mi_teclado, 0, sizeof(mi_teclado));
    strncpy(mi_teclado.name, "Mi teclado", UINPUT_MAX_NAME_SIZE);
    mi_teclado.id.version = 4;
    mi_teclado.id.bustype = BUS_USB;

    ioctl(uinput_keyboard_fd, UI_SET_EVBIT, EV_KEY); // Evento de presionar la tecla
    ioctl(uinput_keyboard_fd, UI_SET_EVBIT, EV_REL); // Evento de soltar la tecla

    // Registo de las teclas que se desean usar
    for (int i = 0; i < 256; i++) {
            ioctl(uinput_keyboard_fd, UI_SET_KEYBIT, i);
    }
        
    // Registo del teclado en el subsistema
    write(uinput_keyboard_fd, &mi_teclado, sizeof(mi_teclado));
       
    // Creación del teclado
    if (ioctl(uinput_keyboard_fd, UI_DEV_CREATE)) {
        perror("No tengo permiso para crear teclado virtual\n");
        close(uinput_keyboard_fd);
        return -1;
    }
    return 1;

}

int send_event_keyboard(int type, int code, int value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    if (write(uinput_keyboard_fd, &event, sizeof(event)) != sizeof(event)) {
        perror("Error al enviar el evento\n");
        return -1;
    }
    return 0;
}

int uinput_mouse_init(void) {
	struct uinput_user_dev dev;
	int i;

	uinput_mouse_fd = open("/dev/uinput", O_WRONLY);
	if (uinput_mouse_fd <= 0) {
		perror("Error opening the uinput device\n");
		return -1;
	}
	memset(&dev,0,sizeof(dev));
	strncpy(dev.name, "mouse", UINPUT_MAX_NAME_SIZE);
	dev.id.version = 4;
	dev.id.bustype = BUS_USB;
	dev.id.product = 1;
	ioctl(uinput_mouse_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_mouse_fd, UI_SET_EVBIT, EV_REL);
	ioctl(uinput_mouse_fd, UI_SET_RELBIT, REL_X);
	ioctl(uinput_mouse_fd, UI_SET_RELBIT, REL_Y);
	ioctl(uinput_mouse_fd, UI_SET_KEYBIT, BTN_MOUSE);
	ioctl(uinput_mouse_fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(uinput_mouse_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput_mouse_fd, UI_SET_KEYBIT, BTN_MIDDLE);

	for (i=0; i<256; i++) {
		ioctl(uinput_mouse_fd, UI_SET_KEYBIT, i);
	}

	write(uinput_mouse_fd, &dev, sizeof(dev));
	if (ioctl(uinput_mouse_fd, UI_DEV_CREATE))
	{
		printf("Unable to create UINPUT device.");
		return -1;
	}
	return 1;
}

void uinput_mouse_move_cursor(int x, int y) {
	float theta;
	struct input_event event;

	// Obtiene el angulo de movimiento
    if (x == 0 && y == 0)
    	theta = 0;
    else if  (x == 0 && y < 0)
    	theta = -M_PI/2;
    else if  (x == 0 && y > 0)
    	theta = M_PI/2;
    else if  (x > 0 && y == 0)
    	theta = 0;
    else if  (x < 0 && y == 0)
    	theta = M_PI;
	else
       theta = atan(y/x);

    // Correción cuarto de la pantalla inferior izquierda
    if (x < 0 && y > 0)
    	theta += M_PI;

	// Correción cuarto de la pantalla superior izquierda
	if (x < 0 && y < 0)
    	theta += M_PI;

	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_REL;
	event.code = REL_X;
	event.value = cos(theta)*10;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_REL;
	event.code = REL_Y;
	event.value = sin(theta)*10;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
}


void press_right() {
	struct input_event event;
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
    event.code =  BTN_RIGHT;
	event.value = 1;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
}


void release_right() {
	struct input_event event;
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
    event.code =  BTN_RIGHT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
}


void press_left() {
	struct input_event event;
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
	event.code = BTN_LEFT;
	event.value = 1;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
}


void release_left() {
	struct input_event event;
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
	event.code = BTN_LEFT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_mouse_fd, &event, sizeof(event));
}


int main(int argc, char *argv[]) {
	int joystick_status;
    int keyboard_status;
	int id;
    int key;
	int estado = INICIAL;
	int mueve;
	int contador;
    using namespace std;
    vector<vector<int>> macros {{},{},{},{}};
    vector<int> macro;

	if (open_joystick() < 0) {

		printf("open failed.\n");
		exit(1);
	}

	if (uinput_mouse_init() < 0)
	{
		printf("open failed.\n");
		exit(2);
	}

	if (uinput_keyboard_init() < 0)
	{
		printf("open failed.\n");
		exit(2);
	}

	mueve = 0;

	while (1) {
		usleep(1000);
		if (mueve)
		{
			if (contador > 30)
			{
			   uinput_mouse_move_cursor(coord._x,coord._y);
			   contador = 0;
			}
			contador++;
		}
		joystick_status = get_joystick_status(&id);
        

        if (estado == PROGRAMANDO){
            keyboard_status = get_keyboard_status(macro);
        }

		if (joystick_status <= 0)
			continue;
		if (joystick_status == JS_EVENT_BUTTON)
		{
			if (id == 5)  // Botón trasero superior derecho
			{
				if (button[id] == 1)
                    press_right();
				else
                    release_right();
			}
			else if (id == 4)  // Botón superior izquierdo
			{
				if (button[id] == 1)
					press_left();
				else
					release_left();
			}
			else if (id == 6){  // Botón trasero inferior izquierdo
				if (button[id] == 1){
					// Iniciamos la programación de la macro
					estado = PROGRAMANDO;
					printf("\nLa programación de la macro ha comenzado.\n");

                    // Inicializamos el vector macro
                    macro.clear();

                    // Abrimos el teclado
                    if (open_keyboard() < 0) {
                        printf("open failed.\n");
                        exit(1);
                    }
				}
			}
			else if (id == 7){  // Botón trasero inferior derecho
				if (button[id] == 1 && estado == PROGRAMANDO){
					// Finalizamos la programación de la macro
					estado = ESPERANDO_BOTON_PROGRAMACION;
					printf("\nLa programación ha terminado. Pulse el botón de programación deseado.\n");
                    close_keyboard();  // Cerramos el teclado
				}
			}
			else if (id >= 0 && id <= 3){  // Pulsación del botón programable
				if (button[id] == 1){
					if (estado == ESPERANDO_BOTON_PROGRAMACION){
						estado = INICIAL;
						printf("Macro guardada en el botón de programación %c.\n", BOTONES_PROGRAMABLES[id]);
                        macros[id] = macro;
					}
					else if (estado == INICIAL){
						printf("\nEjecutando la macro guardada en %c.\n", BOTONES_PROGRAMABLES[id]);
                        if (macros[id].size() == 0){
                            printf("Todavía no se ha guardado ninguna macro en este botón.\n");
                        }
                        else{
                            for (int i=0;i<macros[id].size();i++){
                                send_event_keyboard(EV_KEY, macros[id][i], 1);
                                send_event_keyboard(EV_KEY, macros[id][i], 0);
                                send_event_keyboard(EV_SYN, SYN_REPORT,0);
                                usleep(100000);
                            }
                        }
					}
				}
			}
		}
		else if (joystick_status == JS_EVENT_AXIS)
		{
            // Captura información del controlador digital
			if (coord._x != 0 || coord._y != 0)
				mueve = 1;
			else
				mueve = 0;
		}
	}
}


