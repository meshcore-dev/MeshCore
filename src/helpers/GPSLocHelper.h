#pragma once

class GPSLocHelper {
  static constexpr double APPROX_RADIUS  =  6371e3;   // 6,371 km

  static double toRadians(float degrees) {
    return degrees * PI / 180;
  }

public:
  static float calcGreatArcMeters(float lat1, float lon1, float lat2, float lon2) {
    double diffLati = toRadians(lat1 - lat2);
    double diffLong = toRadians(lon1 - lon2);
    double radiusStartLati = toRadians(lat2);
    double radiusEndLati = toRadians(lat1);

    // A and C are the 'sides' from the spherical triangle.
    double sinLati = sin(diffLati / 2);
    double sinLong = sin(diffLong / 2);
    
    double a = sinLati*sinLati + sinLong*sinLong * cos(radiusStartLati) * cos(radiusEndLati);
    double c = 2 * asin(sqrt(a));

    return APPROX_RADIUS * c;
  }
};
