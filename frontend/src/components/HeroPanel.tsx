import { Box, Typography, Container, Button, Stack } from '@mui/material';

const HeroPanel = () => {
  return (
    <Box
      sx={{
        width: '100%',
        color: 'white',
        pt: 12,
        pb: 8,
        background: 'linear-gradient(135deg, #2c3e50 0%, #1a252f 100%)',
        m: 0,
      }}
    >
      <Container maxWidth="lg">
        <Typography
          component="h1"
          variant="h2"
          align="center"
          gutterBottom
          sx={{ fontWeight: 'bold' }}
        >
          Arnav Rawat
        </Typography>
        <Typography variant="h5" align="center" paragraph sx={{ opacity: 0.9 }}>
          Software Engineer | ML Researcher | UIUC Alum
        </Typography>
        <Stack
          sx={{ pt: 4 }}
          direction="row"
          spacing={2}
          justifyContent="center"
        >
          <Button variant="contained" color="secondary" href="mailto:rawat.arnav@gmail.com">
            Contact Me
          </Button>
          <Button variant="outlined" sx={{ color: 'white', borderColor: 'white' }} href="https://github.com/arawaaa">
            GitHub
          </Button>
        </Stack>
      </Container>
    </Box>
  );
};

export default HeroPanel;
