#include "Ray.h"
#include "Sphere.h"
#include "Mesh.h"
#include "PhotonMap.h"
#define INV_SQRT_3 0.577350269
extern void defaultShader(Ray &ray);
extern SceneIO *scene;
extern std::vector<Primitive*> objects;
extern std::vector<Mesh*> areaLights;
extern std::vector<LightIO*> lights;
extern PhotonMap pMap;
size_t Ray::counter = 0;

#define GLOBAL_PHOTON_COUNT 1000000

float randf(){
    return ((float)rand()/(float)RAND_MAX);
}

float sgn(float x){
    return (x >= 0)*2-1;
}
Ray::Ray(Pos startPosition, Vec3f direction)
:_id(++counter), startPosition(startPosition),
direction(direction.normalize()),
u(0),v(0),
inv_direction(Vec3f(1.0/direction.x, 1.0/direction.y, 1.0/direction.z))
{
    sign[0] = (inv_direction.x < 0);
    sign[1] = (inv_direction.y < 0);
    sign[2] = (inv_direction.z < 0);
    surfaceShader = defaultShader;
    t_max = INFINITY;
    color = BACKGROUND_COLOR;
}

Pos Ray::intersectionPoint() const {
    return startPosition + direction * t_max;
}

Colr Ray::trace(int bounces){
    return pathTrace(bounces, std::unordered_set<Primitive*>());
}


Vec3f Ray::uniformSampleHemisphere(const Vec3f normal)
{
    float u1 = randf(), u2 = randf();
    const float r = sqrt(1.0f - u1 * u1);
    const float phi = 2 * M_PI * u2;
    Vec3f randVector = Vec3f(cos(phi) * r, sin(phi) * r, u1);
    float sign = sgn(Vec3f::dot(randVector, normal));
    return randVector * sign;
}



Vec3f Ray::cosineSampleHemisphere(const Vec3f &direction){
    // Transform into basis of `direction`
    Vec3f X , Y, Z;
    Vec3f::getBasis(direction, X, Y, Z);


    float u1 = randf(), u2 = randf();
    const float r = sqrt(u1);
    const float theta = 2 * M_PI * u2;
    const float x = r * cos(theta);
    const float y = r * sin(theta);
    Vec3f R = Vec3f(x, y, sqrt(fmax(0.0f, 1 - u1))); // Cosine weighted random vector in unit hemisphere.

    return Vec3f(
                 X.x*R.x + Y.x*R.y + Z.x * R.z,
                 X.y*R.x + Y.y*R.y + Z.y * R.z,
                 X.z*R.x + Y.z*R.y + Z.z * R.z);

    }

Pos randomPointOnTriangle(const Mesh* mesh){
    Triangle triangle = *mesh->triangles[rand()%mesh->triangles.size()];
    float r1 = 1.0, r2 = 1.0;
    // Annoying rejection sampling :(
    while(r1 + r2 > 1.0){
        r1 = randf();
        r2 = randf();
    }
    Colr result = triangle.p0 + triangle.u * r1 + triangle.v * r2;
    return result;
}

float surfaceArea(const Mesh* mesh){
    float area = 0;
    for (auto triangle: mesh->triangles){
        area += triangle->area();
    }
    return area;
}

float attenuationFactorAreaLight(const float distance) {
    float c1 = 0.25;
    float c2 = 0.1;
    float c3 = 0.01;
    return fmin(1.0, 1.0 / (c1 + c2*distance + c3*distance));
}



PhotonMap Ray::buildPhotonMap(){
    std::cout << "Generating Global Photon map (" << GLOBAL_PHOTON_COUNT << " photons)..." << std::endl;
    PhotonMap photonMap = PhotonMap();
    
    for ( int i = 0; i < GLOBAL_PHOTON_COUNT; i++){
        Mesh * light = areaLights[random()%areaLights.size()];
        float LightSurfaceArea = surfaceArea(light);
        Colr color = light->materials[0].emissColor;
        color = color * (LightSurfaceArea / (float)GLOBAL_PHOTON_COUNT);
        Pos origin = randomPointOnTriangle(light);
        Vec3f direction = uniformSampleHemisphere(Vec3f(light->normals[0]) * -1.0);

        Ray r = Ray(origin, direction);
        r.photonTrace(color, photonMap, 10);
    }
    photonMap.build();

    return photonMap;
}

void Ray::photonTrace(Colr flux, PhotonMap &photonMap, const int bounces){
    if(bounces <= 0){ return; }
    for ( Primitive* object : objects ) {
        object->intersect(*this);
    }
    if(t_max == INFINITY){ // No hit.
        return;
    }

    /* Diffuse + specular should sum to max 1. */
    float diffuseProb = Colr(material.diffColor).length() * INV_SQRT_3;
    float specularProb = Colr(material.specColor).length() * INV_SQRT_3;
    float absorbProb = 1.0 - diffuseProb + specularProb;
    float transProb = material.ktran;

    float r = randf();
    if (r < transProb){
        refractionRay(intersectionPoint(), IOR_AIR, IOR_GLASS).photonTrace(flux, photonMap, bounces-1);
        return;
    }

    if( r < diffuseProb){
        //diffuse
        Photon p = Photon(intersectionPoint(), direction, flux);
        photonMap.store(p);
        Vec3f newDirection = cosineSampleHemisphere(intersectionNormal);
        Colr newFlux = flux * Vec3f(material.diffColor).normalizeColor();
        Ray(intersectionPoint(), newDirection).photonTrace(newFlux, photonMap, bounces-1);
    }
    else if ( r < diffuseProb + specularProb){
        reflectionRay(intersectionPoint()).photonTrace(flux, photonMap, bounces-1);
    }
    else {
        //absorb
        return;
    }
}

Colr computeRadiance(const Pos &point, const Vec3f &normal, const int numPoints){
    std::priority_queue<Result> photons = pMap.kNN(point, numPoints);
    float radius = photons.top().dx;


    Colr radiance = Colr(0,0,0);
    while(!photons.empty()){
        Photon p = *(photons.top().photon);
        Vec3f incident = p.incidentDirection;
        radiance += p.flux * fmax(0, Vec3f::dot(incident, normal));
        photons.pop();
    }
    return radiance * (1.0/ radius);
}

Colr Ray::pathTrace(int bounces, std::unordered_set<Primitive*> insideObjects){
    Colr result = Colr(0,0,0);
    if(bounces < 0){ return result; }
    for ( Primitive* object : objects ) {
        object->intersect(*this);
    }
    if(t_max == INFINITY){ // No hit.
        return BACKGROUND_COLOR;
    }
    if(material.emissColor[0] > 0){
        return Colr(material.emissColor);
    }
    defaultShader(*this);


    Colr diffuse, reflected, transmitted;


    Colr radiance = computeRadiance(intersectionPoint(), intersectionNormal, 200);
    diffuse = Vec3f(material.diffColor) * radiance;
    if (material.specColor[0] > 0){
        // Specular reflection
        reflected = reflection(intersectionPoint(), bounces-1, insideObjects);
    }
    if (material.ktran > 0) {

        //refraction
        // Transmitted
        bool isInside = insideObjects.size() > 0;
        bool enteringCurrentObject = insideObjects.find(currentObject) == insideObjects.end();
        if(!enteringCurrentObject){
            intersectionNormal = intersectionNormal * -1.0;
        }

        std::unordered_set<Primitive*> mySet(insideObjects);
        if(enteringCurrentObject){
            mySet.insert(currentObject);
        }
        else {
            mySet.erase(currentObject);
        }
        float ior_a = isInside ? IOR_GLASS : IOR_AIR;
        float ior_b = mySet.size() != 0 ? IOR_GLASS : IOR_AIR;
        transmitted = refraction(intersectionPoint(), bounces-1, ior_a, ior_b, insideObjects, insideObjects);
    }
    return diffuse + reflected + transmitted;
}



Colr Ray::indirectLight(const Vec3f dir, const int bounces, const std::unordered_set<Primitive*> insideObjects){
    Ray indirectray = Ray(intersectionPoint(), dir);
    Colr indirectLight = indirectray.pathTrace(bounces-1, insideObjects) * Colr(material.diffColor);

//    float attenuation = attenuationFactorAreaLight((indirectray.intersectionPoint() - intersectionPoint()).length());
    float attenuation = 1;
    return indirectLight * attenuation;
}


Colr Ray::directLight(){
    Colr diffuseColor;
    Mesh * light = areaLights[random()%areaLights.size()];
    Colr color = light->materials[0].emissColor;
    Vec3f lightDirection = (randomPointOnTriangle(light) - intersectionPoint());

    float lightDistance = lightDirection.length();
    lightDirection = lightDirection.normalize();
    float attenuationFactor = attenuationFactorAreaLight(lightDistance);


    Vec3f shadowFactor = areaShadow(lightDirection, lightDistance, light);
    Vec3f directLight = diffuse(lightDirection, color) * shadowFactor * attenuationFactor;
    return directLight;
}

Colr Ray::areaShadow(const Vec3f &L, const float lightDistance, Mesh* light) const {
    Colr shadowFactor = Colr(1,1,1);
    for( auto object : objects){
        Ray shadowRay = Ray(intersectionPoint() + intersectionNormal*BUMP_EPSILON , L);
        if(object->intersect(shadowRay)){
            if(shadowRay.currentObject == light){ continue; }
            Vec3f intersectVector = shadowRay.intersectionPoint() - shadowRay.startPosition;
            if( (intersectVector.length() >= lightDistance) ){ continue; }
            if(shadowRay.material.ktran < 0.001f){return Colr(0,0,0);}
            shadowFactor = shadowFactor * (Colr(shadowRay.material.diffColor).normalizeColor()) * shadowRay.material.ktran;
        }
    }
    return shadowFactor;
}


Colr Ray::shadow(const Vec3f &L, const float lightDistance) const {
    Colr shadowFactor = Colr(1,1,1);
    for( auto object : objects){
        Ray shadowRay = Ray(intersectionPoint() + intersectionNormal*BUMP_EPSILON , L);
        if(object->intersect(shadowRay)){
            if(shadowRay.material.emissColor[0] > 0){ continue; }
            Vec3f intersectVector = shadowRay.intersectionPoint() - shadowRay.startPosition;
            if( (intersectVector.length() >= lightDistance) ){ continue; }
            if(shadowRay.material.ktran < 0.001f){return Colr(0,0,0);}
            shadowFactor = shadowFactor * (Colr(shadowRay.material.diffColor).normalizeColor()) * shadowRay.material.ktran;
        }
    }
    return shadowFactor;
}

Colr Ray::diffuse(const Vec3f &L, const Colr &lightColor) const {
    Colr result = Colr(material.diffColor) * fabs(Vec3f::dot(L, intersectionNormal)) * lightColor;
    return result * (1.0-material.ktran);
}



Colr Ray::traceeee(int bounces, std::unordered_set<Primitive*> insideObjects){
    if(bounces <= 0){return Colr(0,0,0);}
    // Find which object we intersect closest:
    for ( Primitive* object : objects ) {
        object->intersect(*this);
    }
    if(t_max == INFINITY){ // No hit.
        return BACKGROUND_COLOR;
    }

    // We hit something, and have aquired it's material. Run surface shader
    defaultShader(*this);

    // Keep track of which objects we have crossed into, for refraction rays etc..
    // Are we currently inside an object?
    bool isInside = insideObjects.size() > 0;
    // currentObject is a pointer to the closest object we are intersecting.
    // If the set contains it, we are already inside it.
    bool enteringCurrentObject = insideObjects.find(currentObject) == insideObjects.end();

    if(!enteringCurrentObject){
        intersectionNormal = intersectionNormal * -1.0;
    }

    /* Figure out the color to return: */

    Colr ambientColor = ambient();

    /* Per light stuff: diffuse, specular, shadow: */
    Colr diffuseColor, specularColor;
    float attenuation;
    for (LightIO* light: lights){
        Colr color = Colr(light->color);
        Vec3f lightDirection;
        float lightDistance;
        if(light->type == POINT_LIGHT){
            lightDirection = (Vec3f(light->position) - intersectionPoint()); // Vector from the point to the light.
            lightDistance = lightDirection.length();
            lightDirection.normalize();

        } else if (light->type == DIRECTIONAL_LIGHT){
            lightDirection = (Vec3f(light->direction)*-1.0).normalize();
            lightDistance = INFINITY;
        }else{
            std::cout << "Error: Unsupported light type!" << std::endl;
            return Colr(1,0,1);
        }
        attenuation = attenuationFactor(intersectionPoint(), light);
        Vec3f shadowFactor = shadow(lightDirection, lightDistance);
        diffuseColor += diffuse(lightDirection, color) * shadowFactor * attenuation;
        specularColor += specular(lightDirection, color) * shadowFactor * attenuation;
    }

    Colr reflectionColor = Colr(0,0,0);
    if(isReflective()) {
        reflectionColor = reflection(intersectionPoint(), bounces-1, insideObjects);
    }

    Colr refractionColor = Colr(0,0,0);
    if(isTransparent()){
        // If we are entering a new object, add it to the set.
        std::unordered_set<Primitive*> mySet(insideObjects);
        if(enteringCurrentObject){
            mySet.insert(currentObject);
        }
        else {
            mySet.erase(currentObject);
        }
        float ior_a = isInside ? IOR_GLASS : IOR_AIR;
        float ior_b = mySet.size() != 0 ? IOR_GLASS : IOR_AIR;
        refractionColor = refraction(intersectionPoint(), bounces-1, ior_a, ior_b, mySet, insideObjects);
    }
    Colr emisColor = Colr(material.emissColor);
    Colr result = ambientColor + diffuseColor + specularColor + reflectionColor + refractionColor + emisColor;
    result.capColor();
    return result;
}

bool Ray::isReflective() const {
    return (material.specColor[0] > 0.0
            || material.specColor[1] > 0.0
            || material.specColor[2] > 0.0) && t_max < INFINITY;
}

bool Ray::isTransparent() const {
    return material.ktran >= 0.001 && t_max < INFINITY;
}

Ray Ray::reflectionRay(const Pos point) const {
    Vec3f incident = Vec3f::normalize(direction);
    double cosI = -Vec3f::dot(intersectionNormal, incident);
    Vec3f reflectedDirection =  incident + intersectionNormal * cosI * 2;
    return Ray(point + intersectionNormal*BUMP_EPSILON, reflectedDirection);

}

Colr Ray::reflection(const Pos point, const int bounces, const std::unordered_set<Primitive*> mySet) const {
    Vec3f incident = Vec3f::normalize(direction);
    double cosI = -Vec3f::dot(intersectionNormal, incident);
    Vec3f reflectedDirection =  incident + intersectionNormal * cosI * 2;
    Ray reflectionRay = Ray(point + intersectionNormal*BUMP_EPSILON, reflectedDirection);
    Colr reflectionColor = reflectionRay.pathTrace(bounces, mySet);
    return reflectionColor;
}

Ray Ray::refractionRay(const Pos point, const float ior_a, const float ior_b) const {
    Vec3f incident = direction * -1.0;
    float n = ior_b / ior_a;
    float cosThetaI = Vec3f::dot(incident, intersectionNormal);
    float thetaI = acos(cosThetaI);
    float sinThetaT = (ior_a/ior_b) * sin(thetaI);
    float thetaT = asin(sinThetaT);
    float cosThetaT = cos(thetaT);

    Vec3f newDirection = incident * -(1.0/n) - intersectionNormal * (cosThetaT - (1.0/n) * cosThetaI);

    if (thetaI >= asin(ior_b/ior_a)) {
        return Ray(intersectionPoint()+intersectionNormal*BUMP_EPSILON, newDirection);
    }
    else{
        return Ray(intersectionPoint() - intersectionNormal * BUMP_EPSILON, newDirection);
    }

}

Colr Ray::refraction(const Pos point, const int bounces, const float ior_a, const float ior_b, const std::unordered_set<Primitive*> mySet, const std::unordered_set<Primitive*> oldSet){
    Vec3f incident = direction * -1.0;
    float n = ior_b / ior_a;
    float cosThetaI = Vec3f::dot(incident, intersectionNormal);
    float thetaI = acos(cosThetaI);
    float sinThetaT = (ior_a/ior_b) * sin(thetaI);
    float thetaT = asin(sinThetaT);
    float cosThetaT = cos(thetaT);

    Vec3f newDirection = incident * -(1.0/n) - intersectionNormal * (cosThetaT - (1.0/n) * cosThetaI);

    if (thetaI >= asin(ior_b/ior_a)) {
        return Ray(intersectionPoint()+intersectionNormal*BUMP_EPSILON, newDirection).pathTrace(bounces-1, oldSet);
    }
    else{
        return Ray(intersectionPoint() - intersectionNormal * BUMP_EPSILON, newDirection).pathTrace(bounces-1, mySet);
    }
}


Colr Ray::specular(const Vec3f &L, Colr &color) const {
    float q = material.shininess * 30.0;
    Colr Ks = Colr(material.specColor);
    Vec3f V = direction*(-1.0); // Incident flipped - ray from point to eye. Normalized.
    Vec3f Q = intersectionNormal * Vec3f::dot(intersectionNormal, L);
    Vec3f R = ((Q * 2.0) - L).normalize();
    float dot = fmax(0.0,Vec3f::dot(R, V));
    float pow = powf(dot, q);
    return Ks * pow * color;

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