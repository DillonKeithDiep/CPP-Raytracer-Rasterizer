#include <iostream>
#include <glm/glm.hpp>
#include <SDL.h>
#include "SDLauxiliary.h"
#include "TestModel.h"

using namespace std;
using glm::vec3;
using glm::mat3;
using glm::vec2;
using glm::ivec2;

/* ----------------------------------------------------------------------------*/
/* GLOBAL VARIABLES                                                            */

const int SCREEN_WIDTH = 500;
const int SCREEN_HEIGHT = 500;
SDL_Surface* screen;
int t;
float focalLength = 250.0f;

vec3 cameraPos( 0, 0, -2.0f );
mat3 cameraRot = mat3(0.0f);
float yaw = 0; // Yaw angle controlling camera rotation around y-axis

vec3 currentColor;
vec3 currentNormal;
vec3 currentReflectance;

float depthBuffer[SCREEN_HEIGHT][SCREEN_WIDTH];

vec3 lightPos(0,-0.5,0.5f);
vec3 lightPower = 14.0f*vec3( 1, 1, 1 );
vec3 indirectLightPowerPerArea = 0.5f*vec3( 1, 1, 1 );

/* ----------------------------------------------------------------------------*/
/* FUNCTIONS                                                                   */
vector<Triangle> triangles;

void Update();
void Draw();
void VertexShader( const vec3& v, Pixel& p );
void Interpolate( ivec2 a, ivec2 b, vector<ivec2>& result );
void Interpolate( Pixel a, Pixel b, vector<Pixel>& result );
void DrawLineSDL( SDL_Surface* surface, Pixel a, Pixel b, vec3 color );
void DrawPolygonEdges( const vector<vec3>& vertices );
void ComputePolygonRows( const vector<Pixel>& vertexPixels, vector<Pixel>& leftPixels, vector<Pixel>& rightPixels );
void DrawRows( const vector<Pixel>& leftPixels, const vector<Pixel>& rightPixels );
void DrawPolygon( const vector<Vertex>& vertices );
void PixelShader( const Pixel& p );

int main( int argc, char* argv[] )
{
	screen = InitializeSDL( SCREEN_WIDTH, SCREEN_HEIGHT );
	t = SDL_GetTicks();	// Set start value for timer.

	// Generate the Cornell Box
	LoadTestModel( triangles );

	cameraRot[1][1] = 1.0f;

	while( NoQuitMessageSDL() )
	{
		Update();
		Draw();
	}

	SDL_SaveBMP( screen, "screenshot.bmp" );
	return 0;
}

void Update()
{
	// Compute frame time:
	int t2 = SDL_GetTicks();
	float dt = float(t2-t);
	t = t2;
	cout << "Render time: " << dt << " ms." << endl;

	// Clear the screen before drawing the next frame
	for( int y=0; y<SCREEN_HEIGHT; ++y )
	{
		for( int x=0; x<SCREEN_WIDTH; ++x )
		{
			vec3 color( 0.0, 0.0, 0.0 );
			depthBuffer[y][x] = 0;
			PutPixelSDL( screen, x, y, color );
		}
	}

	// Adjust camera transform
	vec3 right(cameraRot[0][0], cameraRot[0][1], cameraRot[0][2]);
	vec3 down(cameraRot[1][0], cameraRot[1][1], cameraRot[1][2]);
	vec3 forward(cameraRot[2][0], cameraRot[2][1], cameraRot[2][2]);
	Uint8* keystate = SDL_GetKeyState( 0 );

	if( keystate[SDLK_UP] )
	{
		// Move camera forward
		cameraPos += 0.1f*forward;
	}
	else if( keystate[SDLK_DOWN] )
	{
		// Move camera backward
		cameraPos -= 0.1f*forward;
	}
	if( keystate[SDLK_LEFT] )
	{
		// Rotate camera to the left
		yaw += 0.1f;
	}
	else if( keystate[SDLK_RIGHT] )
	{
		// Rotate camera to the right
		yaw -= 0.1f;
	}

	// Light movement controls
	if (keystate[SDLK_w])
	{
		lightPos.z += 0.1f;
	}
	else if (keystate[SDLK_s])
	{
		lightPos.z -= 0.1f;
	}

	if (keystate[SDLK_a])
	{
		lightPos.x -= 0.1f;
	}
	else if (keystate[SDLK_d])
	{
		lightPos.x += 0.1f;
	}

	// Update camera rotation matrix
	float c = cos(yaw);
	float s = sin(yaw);
	cameraRot[0][0] = c;
	cameraRot[0][2] = s;
	cameraRot[2][0] = -s;
	cameraRot[2][2] = c;
}

void Draw()
{
	if( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen);

	for( size_t i = 0; i < triangles.size(); ++i )
	{
		// Get the 3 vertices of the triangle
		vector<Vertex> vertices(3);
		vertices[0].position = triangles[i].v0;
		vertices[1].position = triangles[i].v1;
		vertices[2].position = triangles[i].v2;

		// Update global variables for the pixel shader
		currentColor = triangles[i].color;
		currentNormal = triangles[i].normal;
		currentReflectance = vec3(1.0f,1.0f,1.0f);

		DrawPolygon( vertices );
	}

	if( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen);

	SDL_UpdateRect( screen, 0, 0, 0, 0 );
}

// Compute 2D screen position from 3D position
void VertexShader( const Vertex& v, Pixel& p )
{
	// We need to adjust the vertex positions based on the camera position and rotation
	Vertex v2(v);
	v2.position = (v.position - cameraPos) * cameraRot;

	// Store the 3D position of the vertex in the pixel. Divide by the Z value so the value interpolates correctly over the perspective
	p.pos3d = v2.position / v2.position.z;

	// Calculate depth of pixel, inversed so the value interpolates correctly over the perspective
	p.zinv = 1.0f / v2.position.z;

	// Calculate 2D screen position and place (0,0) at top left of the screen
	p.x = int(focalLength * (v2.position.x * p.zinv)) + (SCREEN_WIDTH / 2.0f);
	p.y = int(focalLength * (v2.position.y * p.zinv)) + (SCREEN_HEIGHT / 2.0f);

	// Calculate vertex lighting. Legacy code.
	/*
	float r = glm::distance(v.position, lightPos);
	float A = 4*M_PI*(r*r);
	
	vec3 rDir = glm::normalize(lightPos - v.position);
	vec3 nDir = v.normal;

	vec3 D = (lightPower * max(glm::dot(rDir,nDir), 0.0f)) / A;

	p.illumination = v2.reflectance * (D + indirectLightPowerPerArea) * currentColor;
	*/

}

// Calculate per pixel lighting
void PixelShader( const Pixel& p )
{
	int x = p.x;
	int y = p.y;

	// Multiply pixel 3d position by the z value to get the origin position from the inverse
	vec3 pPos3d(p.pos3d);
	pPos3d *= p.pos3d.z;

	// Calculate lighting
	float r = glm::distance(pPos3d, lightPos);
	float A = 4*M_PI*(r*r);
	
	vec3 rDir = glm::normalize(lightPos - pPos3d);
	vec3 nDir = currentNormal;

	vec3 D = (lightPower * max(glm::dot(rDir,nDir), 0.0f)) / A;

	vec3 pixelColor = currentReflectance * (D + indirectLightPowerPerArea) * currentColor;

	PutPixelSDL( screen, x, y, pixelColor );
}

// Draws a line between two points
void DrawLineSDL( SDL_Surface* surface, Pixel a, Pixel b, vec3 color )
{
	Pixel delta = a - b;
	PixelAbs(delta);
	
	int pixels = max( delta.x, delta.y ) + 1;

	vector<Pixel> line( pixels );

	Interpolate( a, b, line );

	for(int i = 0; i < pixels; ++i)
	{
		// Ensure pixel is on the screen and is closer to the camera than the current value in the depth buffer
		if(line[i].y < SCREEN_HEIGHT && line[i].y >= 0 && line[i].x < SCREEN_WIDTH && line[i].x >= 0 && line[i].zinv > depthBuffer[line[i].y][line[i].x])
		{
			depthBuffer[line[i].y][line[i].x] = line[i].zinv;
			PixelShader(line[i]);
		}
	}
}

// Interpolates between two 2D vectors
void Interpolate( ivec2 a, ivec2 b, vector<ivec2>& result )
{
	int N = result.size();

	vec2 step = vec2(b-a);
	step = step / float(max(N-1,1));
	
	vec2 current( a );

	for( int i=0; i<N; ++i )
	{
		result[i] = current;
		current += step;
	}
}

// Interpolates between two Pixels
void Interpolate( Pixel a, Pixel b, vector<Pixel>& result )
{
	int N = result.size();
	Pixel delta = b-a;

	fPixel step(delta);

	step = (step / float(max(N-1,1)));

	fPixel current( a );

	for( int i=0; i<N; ++i )
	{
		result[i].x = current.x;
		result[i].y = current.y;
		result[i].zinv = current.zinv;
		result[i].pos3d = current.pos3d;
		current.x += step.x;
		current.y += step.y;
		current.zinv += step.zinv;
		current.pos3d += step.pos3d;
	}
}

void ComputePolygonRows( const vector<Pixel>& vertexPixels, vector<Pixel>& leftPixels, vector<Pixel>& rightPixels )
{
	// 1. Find max and min y-value of the polygon
	// and compute the number of rows it occupies.

	int maxY = max(max(vertexPixels[0].y, vertexPixels[1].y), vertexPixels[2].y);
	int minY = min(min(vertexPixels[0].y, vertexPixels[1].y), vertexPixels[2].y);

	int ROWS = maxY - minY + 1;

	// 2. Resize leftPixels and rightPixels
	// so that they have an element for each row.

	leftPixels.resize( ROWS );
	rightPixels.resize( ROWS );

	// 3. Initialize the x-coordinates in leftPixels
	// to some really large value and the x-coordinates
	// in rightPixels to some really small value.

	for( int i = 0; i < ROWS; ++i )
	{
		leftPixels[i].x = +numeric_limits<int>::max();
		rightPixels[i].x = -numeric_limits<int>::max();
	}

	// 4. Loop through all edges of the polygon and use
	// linear interpolation to find the x-coordinate for
	// each row it occupies. Update the corresponding
	// values in rightPixels and leftPixels.

	for(int i = 0; i < 3; i++)
	{
		int j = (i + 1) % 3; // Ensure all 3 edges are looped through
		// Adjust vertex positions to have y value 0 at minY so Y coordinates map to array indicies
		Pixel v1 (vertexPixels[i].x, vertexPixels[i].y - minY, vertexPixels[i].zinv, vertexPixels[i].pos3d);
		Pixel v2 (vertexPixels[j].x, vertexPixels[j].y - minY, vertexPixels[j].zinv, vertexPixels[j].pos3d);

		int edgePixels = abs(vertexPixels[i].y - vertexPixels[j].y) + 1; // Calculate number of rows this edge occupies
		vector<Pixel> edgeResult(edgePixels); // Create array of ivec2 with number of rows
		Interpolate(v1,v2,edgeResult); // Interpolate between the two vertices

		for(int k = 0; k < edgePixels; k++)
		{
			if(edgeResult[k].x < leftPixels[edgeResult[k].y].x)
			{
				leftPixels[edgeResult[k].y].x = edgeResult[k].x;
				leftPixels[edgeResult[k].y].y = edgeResult[k].y + minY;
				leftPixels[edgeResult[k].y].zinv = edgeResult[k].zinv;
				leftPixels[edgeResult[k].y].pos3d = edgeResult[k].pos3d;
			}

			if(edgeResult[k].x > rightPixels[edgeResult[k].y].x)
			{
				rightPixels[edgeResult[k].y].x = edgeResult[k].x;
				rightPixels[edgeResult[k].y].y = edgeResult[k].y + minY;
				rightPixels[edgeResult[k].y].zinv = edgeResult[k].zinv;
				rightPixels[edgeResult[k].y].pos3d = edgeResult[k].pos3d;
			}
		}
	}
}

// Draw a line for each row of the triangle
void DrawRows( const vector<Pixel>& leftPixels, const vector<Pixel>& rightPixels )
{
	for(int i = 0; i < leftPixels.size(); i++)
	{
		DrawLineSDL(screen, leftPixels[i],rightPixels[i],currentColor);
	}
}

void DrawPolygon( const vector<Vertex>& vertices )
{
	int V = vertices.size();
	vector<Pixel> vertexPixels( V );

	for( int i=0; i<V; ++i )
		VertexShader( vertices[i], vertexPixels[i] );

	vector<Pixel> leftPixels;
	vector<Pixel> rightPixels;

	ComputePolygonRows( vertexPixels, leftPixels, rightPixels );
	DrawRows( leftPixels, rightPixels );
}
