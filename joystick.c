#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/uinput.h>
#include <math.h>
#include <linux/joystick.h>

// Identificación del joystick
#define JOYSTICK_DEVNAME "/dev/input/js0"

static int joystick_fd = -1;

typedef struct{
    int _x;
    int _y;
} Coordenadas;

static int uinput_fd = 0;
static Coordenadas coord;

static int num_of_axis=0, num_of_buttons=0;
static char *button=NULL, name_of_joystick[80];

// Abre el joystick en modo lectura, esto evita cambiar los permisos
int open_joystick()
{
	joystick_fd = open(JOYSTICK_DEVNAME, O_RDONLY | O_NONBLOCK);
	if (joystick_fd < 0)
		return joystick_fd;

	// IOCTL permite cambiar y leer la configuracion del dispositivo
	ioctl( joystick_fd, JSIOCGAXES, &num_of_axis );
	ioctl( joystick_fd, JSIOCGBUTTONS, &num_of_buttons );
	ioctl( joystick_fd, JSIOCGNAME(80), &name_of_joystick );
	num_of_axis = num_of_axis & 0xFF;
	num_of_buttons = num_of_buttons & 0xFF;
	printf("Joystick detected: %s\n\t%d axis\n\t%d buttons\n\n", name_of_joystick, num_of_axis, num_of_buttons);

	button = (char *) calloc( num_of_buttons, sizeof( char ) );
	return joystick_fd;
}

int read_joystick_event(struct js_event *jse)
{
	int bytes;

	bytes = read(joystick_fd, jse, sizeof(*jse)); 

	if (bytes == -1)
		return 0;

	if (bytes == sizeof(*jse))
		return 1;

	printf("Unexpected bytes from joystick:%d\n", bytes);

	return -1;
}

void close_joystick()
{
	close(joystick_fd);
}

int get_joystick_status(int *id)
{
	int rc;
	struct js_event jse;
	if (joystick_fd < 0)
		return -1;

	while ((rc = read_joystick_event(&jse) == 1)) {
		jse.type &= ~JS_EVENT_INIT; // Ignora los eventos sintéticos
         printf("time: %9u  value: %6d  type: %3u  number:  %2u\r",
				 jse.time, jse.value, jse.type, jse.number);
		     fflush(stdout);
		if (jse.type == JS_EVENT_AXIS) {
			switch (jse.number) {
			case 1: coord._y = jse.value;  // Control analógico eje vertical
			*id = 0;
			break;
			case 2: coord._x = jse.value;  // Control analógico eje horizontal
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

int uinput_mouse_init(void) {
	struct uinput_user_dev dev;
	int i;

	uinput_fd = open("/dev/uinput", O_WRONLY);
	if (uinput_fd <= 0) {
		perror("Error opening the uinput device\n");
		return -1;
	}
	memset(&dev,0,sizeof(dev)); // Intialize the uInput device to NULL
	strncpy(dev.name, "mouse", UINPUT_MAX_NAME_SIZE);
	dev.id.version = 4;
	dev.id.bustype = BUS_USB;
	dev.id.product = 1;
	ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_fd, UI_SET_EVBIT, EV_REL);
	ioctl(uinput_fd, UI_SET_RELBIT, REL_X);
	ioctl(uinput_fd, UI_SET_RELBIT, REL_Y);
	ioctl(uinput_fd, UI_SET_KEYBIT, BTN_MOUSE);
	ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(uinput_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE);


	for (i=0; i<256; i++) {
		ioctl(uinput_fd, UI_SET_KEYBIT, i);
	}


	write(uinput_fd, &dev, sizeof(dev));
	if (ioctl(uinput_fd, UI_DEV_CREATE))
	{
		printf("Unable to create UINPUT device.");
		return -1;
	}
	return 1;
}

void uinput_mouse_move_cursor(int x, int y )
{
	float theta;
	struct input_event event; // Input device structure

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

    // Correcion cuarto de la pantalla inferior izquierda
    if (x < 0 && y > 0)
    	theta += M_PI;

	// Correcion cuarto de la pantalla superior izquierda
	if (x < 0 && y < 0)
    	theta += M_PI;


	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_REL;
	event.code = REL_X;
	event.value = cos(theta)*10;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_REL;
	event.code = REL_Y;
	event.value = sin(theta)*10;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
}


void press_right()
{
	// Report BUTTON CLICK - PRESS event
	struct input_event event; // Input device structure
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
    event.code =  BTN_RIGHT;
	event.value = 1;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
}


void release_right()
{
	// Report BUTTON CLICK - RELEASE event
	struct input_event event; // Input device structure
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
    event.code =  BTN_RIGHT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
}


void press_left()
{
	// Report BUTTON CLICK - PRESS event
	struct input_event event; // Input device structure
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
	event.code = BTN_LEFT;
	event.value = 1;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
}


void release_left()
{
	// Report BUTTON CLICK - RELEASE event
	struct input_event event; // Input device structure
	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = EV_KEY;
	event.code = BTN_LEFT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
	event.type = EV_SYN;
	event.code = SYN_REPORT;
	event.value = 0;
	write(uinput_fd, &event, sizeof(event));
}


int main(int argc, char *argv[])
{
	int fd;
	int status;
	int id;
	char mueve;
	char contador;

	fd = open_joystick();

	if (fd < 0) {
		printf("open failed.\n");
		exit(1);
	}

	if (uinput_mouse_init()<0)
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
		status = get_joystick_status(&id);

		if (status <= 0)
			continue;
		if (status == JS_EVENT_BUTTON)
		{
			if (id == 5)  // Botón trasero superior derecho
			{
				if (button[id] == 1)
                    press_right();
				else
                    release_right();
			}
			else if (id == 4)  //  trasBotónero superior izquierdo
			{
				if (button[id] == 1)
					press_left();
				else
					release_left();
			}
		}
		else if (status == JS_EVENT_AXIS)
		{
            // Captura información del controlador digital.
			if (coord._x != 0 || coord._y != 0)
				mueve = 1;
			else
				mueve = 0;
		}
	}
}


