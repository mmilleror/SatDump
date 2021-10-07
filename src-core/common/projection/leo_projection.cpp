#include "leo_projection.h"

#include <cmath>
#include "common/predict/predict.h"
#include <functional>
#include "logger.h"

namespace projection
{
    void LEOScanProjector::initCurvatureTable()
    {
        const float EARTH_RADIUS = 6371.0f;
        float satellite_orbit_radius = EARTH_RADIUS + settings.correction_height;                                                                              // Compute the satellite's orbit radius
        corrected_width = round(settings.correction_swath / settings.correction_res);                                                                          // Compute the output image size, or number of samples from the imager
        float satellite_view_angle = settings.correction_swath / EARTH_RADIUS;                                                                                 // Compute the satellite's view angle
        float edge_angle = -atanf(EARTH_RADIUS * sinf(satellite_view_angle / 2) / ((cosf(satellite_view_angle / 2)) * EARTH_RADIUS - satellite_orbit_radius)); // Max angle relative to the satellite

        curvature_correction_factors_inv.reserve(settings.image_width);

        // Generate them
        for (int i = 0; i < corrected_width; i++)
        {
            float angle = ((float(i) / float(corrected_width)) - 0.5f) * satellite_view_angle;                                    // Get the satellite's angle
            float satellite_angle = -atanf(EARTH_RADIUS * sinf(angle) / ((cosf(angle)) * EARTH_RADIUS - satellite_orbit_radius)); // Convert to an angle relative to earth
            float f = settings.image_width * ((satellite_angle / edge_angle + 1.0f) / 2.0f);                                      // Convert that to a pixel from the original image
            curvature_correction_factors_fwd.push_back(f);
            curvature_correction_factors_inv[int(f)] = i;
        }
    }

    void LEOScanProjector::generateProjections()
    {
        // Setup SGP4 model
        predict_orbital_elements_t *satellite_object = predict_parse_tle(settings.sat_tle.line1.c_str(),
                                                                         settings.sat_tle.line2.c_str());
        predict_position satellite_orbit;

        // Projection we're gonna work with
        projection::TPERSProjection pj;

        // Needed to compute Az
        std::function<std::pair<int, int>(float, float, int, int)> toSatCoords = [&pj](float lat, float lon, int map_height, int map_width) -> std::pair<int, int>
        {
            double x, y;
            pj.forward(lon, lat, x, y);

            if (fabs(x) > 1e10f || fabs(y) > 1e10f)
                return {-1, -1};

            float hscale = 4.0;
            float vscale = 4.0;
            int image_x = x * hscale * (map_width / 2.0);
            int image_y = y * vscale * (map_height / 2.0);

            image_x += map_width / 2.0;
            image_y += map_height / 2.0;

            return {image_x, (map_height - 1) - image_y};
        };

        for (int currentScan = 0; currentScan < (int)settings.utc_timestamps.size(); currentScan++)
        {
            double currentTimestamp = settings.utc_timestamps[currentScan] + settings.time_offset;

            // Get Julian time of the scan, with full accuracy and calculate the satellite's
            // position at the time
            predict_julian_date_t currentJulianTime = predict_to_julian_double(currentTimestamp);
            predict_orbit(satellite_object, &satellite_orbit, currentJulianTime);
            poss.push_back(satellite_orbit);
            // Calculate Az to use from the satellite's motion vector
            float az = 0;
            {
                // Get point of view from the sat at the time
                pj.init(satellite_orbit.altitude * 1000, satellite_orbit.longitude * 57.29578, satellite_orbit.latitude * 57.29578, 0, 0);

                predict_position satellite_pos1;
                predict_position satellite_pos2;
                predict_orbit(satellite_object, &satellite_pos1, predict_to_julian_double(currentTimestamp - 200));
                predict_orbit(satellite_object, &satellite_pos2, predict_to_julian_double(currentTimestamp + 200));

                std::pair<float, float> geo_cc1 = toSatCoords(satellite_pos1.latitude * 57.29578, satellite_pos1.longitude * 57.29578, 200, 200);
                std::pair<float, float> geo_cc2 = toSatCoords(satellite_pos2.latitude * 57.29578, satellite_pos2.longitude * 57.29578, 200, 200);

                // This returns the angle from the vector
                az = atan(((geo_cc1.second - geo_cc2.second)) / (geo_cc1.first - geo_cc2.first)) * 57.29578;
            }

            bool invertOffset = az > 0;

            az -= 90;

            // If any Az offset is required
            // This has to be relative to the sat vector so...
            // We swap it out when required
            if (invertOffset)
                az -= settings.az_offset;
            else
                az += settings.az_offset;

            // Get real point of view aligned with the sat's vector
            pj.init(satellite_orbit.altitude * 1000, satellite_orbit.longitude * 57.29578, satellite_orbit.latitude * 57.29578, settings.tilt_offset, az);

            projs.push_back(pj);                                 // Save that projection for later use
            sat_footprints.push_back(satellite_orbit.footprint); // Save footprint
        }
    }

    LEOScanProjector::LEOScanProjector(LEOScanProjectorSettings settings) : settings(settings)
    {
        logger->info("Include curvature table...");
        initCurvatureTable();
        logger->info("Generate projection...");
        generateProjections();
    }

    int LEOScanProjector::inverse(int img_x, int img_y, double &lat, double &lon, bool correct)
    {
        // Check we're in bounds
        if (img_y > (int)projs.size() || img_x >= settings.image_width)
            return 1;

        // Get what we're gonna use
        projection::TPERSProjection &pj = projs[img_y];
        double &footprint = sat_footprints[img_y];

        // Get the width and pixel to use, depending on if we have to do correction or not
        double corr_x = correct ? curvature_correction_factors_inv[img_x] : img_x;
        double width = correct ? corrected_width : settings.image_width;

        // Scale to the projected area
        double proj_x = settings.invert_scan ? ((width - 1) - corr_x) : corr_x;
        proj_x -= width / 2.0;
        proj_x += settings.proj_offset;
        double pjx = proj_x / (settings.proj_scale * (width / 2.0));

        // The instrument has a fixed FOV, so its actual footprint varies with altitude / position
        // We hence scale the input values, relative to a scan angle to actual match that independently
        // of any variations.
        // This has to be done as the projection is not to scale
        pjx *= settings.instrument_swath / footprint;

        return pj.inverse(pjx, 0, lon, lat); // Forward return value
    }
};