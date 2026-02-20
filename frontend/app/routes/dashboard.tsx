import { Container, Typography, Card, CardContent, Grid, Box } from '@mui/material';
import { useState, useEffect } from 'react';

export default function Dashboard() {
  const [temperature, setTemperature] = useState<string>('Loading...');
  const [humidity, setHumidity] = useState<string>('Loading...');

  const fetchData = async () => {
    try {
      const tempRes = await fetch('/temperature');
      const tempText = await tempRes.text();
      setTemperature(tempText.trim());

      const humidityRes = await fetch('/humidity');
      const humidityText = await humidityRes.text();
      setHumidity(humidityText.trim());
    } catch (error) {
      console.error('Error fetching dashboard data:', error);
    }
  };

  useEffect(() => {
    fetchData();
    const interval = setInterval(fetchData, 180000); // 3 minutes
    return () => clearInterval(interval);
  }, []);

  return (
    <Container maxWidth="lg" sx={{ mt: 15, mb: 8 }}>
      <Typography variant="h2" gutterBottom sx={{ fontWeight: 'bold' }}>
        Dashboard
      </Typography>
      <Typography variant="h5" sx={{ color: 'text.secondary', mb: 6 }}>
        Real-time monitoring
      </Typography>

      <Grid container spacing={4}>
        <Grid size={{xs: 12, md: 6}}>
          <Card 
            sx={{ 
              bgcolor: 'background.paper',
              borderRadius: 2,
              boxShadow: (theme) => theme.shadows[2]
            }}
          >
            <CardContent sx={{ p: 4 }}>
              <Typography variant="h5" component="h3" gutterBottom sx={{ fontWeight: 'bold' }}>
                Climate Status
              </Typography>
              <Box sx={{ mt: 3 }}>
                <Box sx={{ display: 'flex', justifyContent: 'space-between', py: 1, borderBottom: '1px solid rgba(255,255,255,0.1)' }}>
                  <Typography variant="body1" sx={{ fontWeight: 'medium' }}>Temperature</Typography>
                  <Typography variant="body1" color="primary" sx={{ fontWeight: 'bold' }}>{temperature}°F</Typography>
                </Box>
                <Box sx={{ display: 'flex', justifyContent: 'space-between', py: 1 }}>
                  <Typography variant="body1" sx={{ fontWeight: 'medium' }}>Humidity</Typography>
                  <Typography variant="body1" color="secondary" sx={{ fontWeight: 'bold' }}>{humidity}%</Typography>
                </Box>
              </Box>
            </CardContent>
          </Card>
        </Grid>
      </Grid>
    </Container>
  );
}
