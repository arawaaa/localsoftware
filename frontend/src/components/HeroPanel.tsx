import { Box, Typography, Container, Button, Stack } from '@mui/material';

const HeroPanel = () => {
  return (
        <Box
          sx={{
            width: '100%',
            color: 'text.primary',
            pt: 12,
            pb: 8,
            background: 'linear-gradient(135deg, #0a1929 0%, #132f4c 100%)',
            m: 0,
            borderBottom: '1px solid rgba(255, 255, 255, 0.08)',
          }}
        >
          <Container maxWidth="lg">
            <Stack 
              direction={{ xs: 'column', md: 'row' }} 
              spacing={4} 
              alignItems="center" 
              justifyContent="space-between"
            >
              <Box sx={{ flex: 1.2 }}>
                <Typography
                  component="h1"
                  variant="h2"
                  gutterBottom
                  sx={{ fontWeight: 'bold', background: 'linear-gradient(45deg, #90caf9 30%, #f48fb1 90%)', WebkitBackgroundClip: 'text', WebkitTextFillColor: 'transparent' }}
                >
                  Arnav Rawat
                </Typography>
                <Typography variant="h5" paragraph sx={{ opacity: 0.9, color: 'text.secondary' }}>
                  Software Engineer | ML Researcher | UIUC Alum
                </Typography>
                <Stack
                  sx={{ pt: 2 }}
                  direction="row"
                  spacing={2}
                >
                  <Button variant="contained" color="primary" href="mailto:rawat.arnav@gmail.com">
                    Contact Me
                  </Button>
                  <Button variant="outlined" sx={{ color: 'text.primary', borderColor: 'rgba(255, 255, 255, 0.23)' }} href="https://github.com/arawaaa">
                    GitHub
                  </Button>
                </Stack>
              </Box>
              <Box sx={{ flex: 0.8, borderLeft: '4px solid #90caf9', pl: 3 }}>
                <Typography variant="body1" sx={{ fontSize: '1.1rem', lineHeight: 1.8, opacity: 0.9, color: 'text.secondary' }}>
                  I'm a natural general intelligence currently living in the <strong style={{ color: '#fff' }}>Chicagoland area</strong>.
                  A Computer Science and Mathematics graduate from the <strong style={{ color: '#fff' }}>University of Illinois Urbana-Champaign</strong>, 
                  I specialize in building scalable software, optimizing algorithms, and exploring the depths of <strong style={{ color: '#fff' }}>Mechanistic Interpretability</strong> 
                  to ensure we can flourish and exist peacefully with our machine peers.
                </Typography>
              </Box>
            </Stack>
          </Container>
        </Box>
    
  );
};

export default HeroPanel;
