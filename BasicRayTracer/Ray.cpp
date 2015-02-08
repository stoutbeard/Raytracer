#include "Ray.h"
#include "Sphere.h"
extern SceneIO *scene;
extern std::vector<Primitive*> objects;
extern std::vector<LightIO*> lights;
size_t Ray::counter = 0;
Ray::Ray(Pos startPosition, Vec3f direction)
:_id(++counter), startPosition(startPosition), direction(direction.normalize())
{
    t_max = INFINITY;
    color = BACKGROUND_COLOR;
}

Pos Ray::intersectionPoint(){
    return startPosition + direction * t_max;
}

/* Placeholder. Expand to handle transparent objects -> colored shadow.
   Perhaps merge with trace?
 */
//bool Ray::shadowTrace(){
//
//}



Colr Ray::trace(int bounces){

    // Find which object we intersect closest:
    for ( Primitive* object : objects ) {
        object->intersect(*this);
    }
    if(t_max == INFINITY){ // No hit.
        return BACKGROUND_COLOR;
    }
    Colr diffuseColor = diffuse(intersectionPoint());
    Colr ambientColor = ambient();
    Colr specularColor = specular(intersectionPoint());
    // Shadow ray

    // Reflection ray
    Colr result = ambientColor + diffuseColor + specularColor;
    result.capColor();
    return result;
}

Colr Ray::diffuse(const Pos point) const {
    Colr result = Colr(0,0,0);
    for(LightIO* light : lights){
        Vec3f directionToLight = (Vec3f(light->position) - point).normalize();
        result = result + Colr(material.diffColor) * fmax(0,Vec3f::dot(directionToLight,intersectionNormal));
        result = result * attenuationFactor(point, light);
    }
    return result * (1-material.ktran);
}

Colr Ray::specular(const Pos point) const {
    Colr result = Colr(0,0,0);
    float q = material.shininess * 20.0;
    Colr Ks = Colr(material.specColor);
    Vec3f V = direction*(-1.0); // Incident flipped - ray from point to eye. Normalized.
    for( auto light:lights) {
        Vec3f L = Vec3f(light->position) - point; // Vector from the point to the light.
        Vec3f Q = intersectionNormal * Vec3f::dot(intersectionNormal, L);
        Vec3f R = ((Q * 2) - L).normalize();
        float dot = fmax(0,Vec3f::dot(R, V));
        float pow = powf(dot, q);
        result = result + Ks * pow * attenuationFactor(point, light);
    }
    return result;
}

float Ray::attenuationFactor(const Pos point, const LightIO* light) const {
    if(light->type == DIRECTIONAL_LIGHT){ return 1.0;}
    float c1 = 0.25;
    float c2 = 0.1;
    float c3 = 0.01;
    float d = (point - Vec3f(light->position)).length(); // distance between light and shaded point.
    return fmin(1.0, 1.0 / (c1 + c2*d + c3*d*d));
}

Colr Ray::ambient() const {
    return Colr(material.diffColor[0] * material.ambColor[0],
                material.diffColor[1] * material.ambColor[1],
                material.diffColor[2] * material.ambColor[2]) * (1.0-material.ktran);
}